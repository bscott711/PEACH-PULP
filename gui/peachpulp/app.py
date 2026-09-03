"""PEACH PULP main window: Live tab (home) + Automated tab, persistent
RUN / SKIP / STOP footer, workspace save/load."""
from __future__ import annotations

from PySide6.QtCore import QTimer
from PySide6.QtGui import QKeySequence, QShortcut
from PySide6.QtWidgets import (
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P
from . import workspace as W
from .automated import AutomatedPanel
from .link import LinkBase
from .live import LivePanel

_UPLOAD_DEBOUNCE_MS = 400


class MainWindow(QMainWindow):
    def __init__(
        self,
        link: LinkBase,
        show_spares: bool = False,
        workspace_name: str | None = None,
        kiosk: bool = False,
    ) -> None:
        super().__init__()
        self.link = link
        self.setWindowTitle("PEACH PULP")
        self._count = P.NUM_PUMPS if show_spares else P.ACTIVE_PUMPS
        self._dirty = False
        self._last: P.Telemetry | None = None

        try:
            self._ws = W.load(workspace_name) if workspace_name else W.load_last_or_default()
        except (OSError, ValueError):
            self._ws = W.default_workspace()

        central = QWidget()
        central.setObjectName("central")
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(14, 10, 14, 10)
        root.setSpacing(8)

        # --- header ---------------------------------------------------
        title = QLabel("PEACH PULP")
        title.setObjectName("title")
        self._status = QLabel("disconnected")
        self._status.setObjectName("statusPill")
        self._status.setProperty("connected", "false")
        top = QHBoxLayout()
        top.addWidget(title)
        top.addStretch(1)
        top.addWidget(self._status)
        if not kiosk:
            exit_btn = QPushButton("Exit")
            exit_btn.setObjectName("iconbtn")
            exit_btn.setMinimumHeight(34)
            exit_btn.clicked.connect(self.close)
            top.addWidget(exit_btn)
        root.addLayout(top)

        # --- tabs ----------------------------------------------------
        self._live = LivePanel(self._count, self._ws.pump_names)
        self._live.speedChanged.connect(self._on_live_speed)
        self._live.enableToggled.connect(
            lambda i, on: self.link.send(P.cmd_enable(i, on))
        )
        self._live.jogRequested.connect(
            lambda i, s: self.link.send(P.cmd_jog(i, s if s else 500))
        )
        self._live.jogStopped.connect(lambda i: self.link.send(P.cmd_jog(i, 0)))

        self._auto = AutomatedPanel()
        self._auto.programChanged.connect(self._on_program_changed)
        self._auto.loadRequested.connect(self._do_load)
        self._auto.saveRequested.connect(self._do_save)
        self._auto.saveAsRequested.connect(self._do_save_as)
        self._auto.newRequested.connect(self._do_new)

        self._tabs = QTabWidget()
        self._tabs.addTab(self._live, "Live")
        self._tabs.addTab(self._auto, "Automated")
        root.addWidget(self._tabs, 1)

        # --- persistent footer: RUN / SKIP / STOP -----------------
        self._run = _btn("RUN", "run", lambda: self.link.send(P.cmd_run()))
        self._skip = _btn("SKIP", "skip", lambda: self.link.send(P.cmd_skip()))
        self._stop = _btn("STOP", "stop", lambda: self.link.send(P.cmd_stop()))
        footer = QHBoxLayout()
        footer.setSpacing(10)
        footer.addWidget(self._run, 2)
        footer.addWidget(self._skip, 1)
        footer.addWidget(self._stop, 2)
        root.addLayout(footer)
        self._run.setEnabled(True)
        self._skip.setEnabled(False)

        # --- log --------------------------------------------------
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        self._log.setFixedHeight(74)
        root.addWidget(self._log)

        # --- link wiring ----------------------------------------
        link.telemetry.connect(self._on_telemetry)
        link.event.connect(lambda e: self._append("event", e))
        link.error.connect(lambda e: self._append("ERR", e))
        link.log.connect(lambda m: self._append("fw", m))
        link.connected.connect(self._on_connected)

        # --- upload debounce ----------------------------------
        self._upload = QTimer(self)
        self._upload.setSingleShot(True)
        self._upload.setInterval(_UPLOAD_DEBOUNCE_MS)
        self._upload.timeout.connect(self._upload_program)

        # --- keyboard escape hatches -------------------------
        QShortcut(QKeySequence("Escape"), self, activated=self._leave_fullscreen)
        QShortcut(QKeySequence("F11"), self, activated=self._toggle_fullscreen)
        QShortcut(QKeySequence("Ctrl+Q"), self, activated=self.close)

        self._apply_ws()

    # ---- workspace lifecycle ----------------------------------------
    def _apply_ws(self) -> None:
        self._live.set_pump_names(self._ws.pump_names)
        self._live.set_speeds(self._ws.live_speeds)
        self._auto.load_program(self._ws.phases, self._ws.pump_names)
        self._dirty = False
        self._refresh_bar()
        self._upload.start()

    def _sync_ws(self) -> None:
        self._ws.phases = self._auto.program()
        self._ws.live_speeds = self._live.speeds()

    def _refresh_bar(self) -> None:
        self._auto.refresh_bar(W.list_workspaces(), self._ws.name, self._dirty)

    def _mark_dirty(self) -> None:
        if not self._dirty:
            self._dirty = True
            self._refresh_bar()

    def _on_program_changed(self) -> None:
        self._mark_dirty()
        self._upload.start()

    def _do_save(self) -> None:
        self._sync_ws()
        W.save(self._ws)
        self._dirty = False
        self._refresh_bar()
        self._append("gui", "saved workspace '{}'".format(self._ws.name))

    def _do_save_as(self) -> None:
        name, ok = QInputDialog.getText(self, "Save workspace as", "Name:", text=self._ws.name)
        if not ok or not name.strip():
            return
        self._sync_ws()
        self._ws.name = name.strip()
        W.save(self._ws)
        self._dirty = False
        self._refresh_bar()
        self._append("gui", "saved workspace '{}'".format(self._ws.name))

    def _do_load(self, name: str) -> None:
        name = (name or "").strip()
        if not name or name == self._ws.name and not self._dirty:
            return
        if self._dirty and not self._confirm_discard():
            self._refresh_bar()
            return
        try:
            self._ws = W.load(name)
        except (OSError, ValueError) as exc:
            self._append("ERR", "load '{}': {}".format(name, exc))
            return
        self._apply_ws()
        self._append("gui", "loaded workspace '{}'".format(self._ws.name))

    def _do_new(self) -> None:
        if self._dirty and not self._confirm_discard():
            return
        self._ws = W.default_workspace()
        self._ws.name = "Untitled"
        self._apply_ws()

    def _confirm_discard(self) -> bool:
        r = QMessageBox.question(
            self,
            "Discard changes?",
            "'{}' has unsaved changes. Discard them?".format(self._ws.name),
            QMessageBox.Discard | QMessageBox.Cancel,
        )
        return r == QMessageBox.Discard

    # ---- program upload ------------------------------------------
    def _upload_program(self) -> None:
        try:
            self.link.send_lines(P.cmd_program(self._auto.program()))
        except Exception as exc:  # noqa: BLE001 - never let a bad edit kill the UI
            self._append("ERR", "upload: {}".format(exc))

    # ---- link -> UI --------------------------------------------
    def _on_connected(self, ok: bool) -> None:
        self._status.setText("connected" if ok else "disconnected")
        self._status.setProperty("connected", "true" if ok else "false")
        st = self._status.style()
        st.unpolish(self._status)
        st.polish(self._status)
        if ok:
            self._upload.start()  # make the firmware match the editor

    def _on_telemetry(self, t: P.Telemetry) -> None:
        self._last = t
        label, total = "", 0
        prog = self._auto.program()
        if 0 <= t.phase < len(prog):
            label, total = prog[t.phase].label, prog[t.phase].seconds
        self._live.apply_telemetry(t, label, total)
        self._auto.apply_telemetry(t, label, total)
        self._run.setEnabled(not t.running)
        self._skip.setEnabled(t.running)

    def _on_live_speed(self, idx: int, steps: int) -> None:
        self.link.send(P.cmd_speed(idx, steps))
        self._mark_dirty()

    # ---- fullscreen / exit ------------------------------------
    def _leave_fullscreen(self) -> None:
        if self.isFullScreen():
            self.showNormal()

    def _toggle_fullscreen(self) -> None:
        self.showNormal() if self.isFullScreen() else self.showFullScreen()

    # ---- misc ----------------------------------------------
    def _append(self, tag: str, msg: str) -> None:
        self._log.appendPlainText("[{}] {}".format(tag, msg))

    def closeEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        try:
            self._sync_ws()
            W.save(self._ws)
        except (OSError, ValueError):
            pass
        self.link.stop()
        super().closeEvent(ev)


def _btn(text: str, name: str, slot) -> QPushButton:
    b = QPushButton(text)
    b.setObjectName(name)
    b.setMinimumHeight(58)
    b.clicked.connect(slot)
    return b
