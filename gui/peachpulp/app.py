"""PEACH PULP main window."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P
from .link import LinkBase
from .widgets import PhasePanel, PumpRow


class MainWindow(QMainWindow):
    def __init__(self, link: LinkBase, show_spares: bool = False) -> None:
        super().__init__()
        self.link = link
        self.setWindowTitle("PEACH PULP")
        self._last_telem: P.Telemetry | None = None

        central = QWidget()
        central.setObjectName("central")
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(18, 14, 18, 14)
        root.setSpacing(12)

        # --- header ------------------------------------------------------
        title = QLabel("PEACH PULP")
        title.setObjectName("title")
        self._status = QLabel("disconnected")
        self._status.setObjectName("statusPill")
        self._status.setProperty("connected", "false")
        top = QHBoxLayout()
        top.addWidget(title)
        top.addStretch(1)
        top.addWidget(self._status)
        root.addLayout(top)

        # --- pumps (left, scrollable) + phases (right) ------------------
        mid = QHBoxLayout()
        mid.setSpacing(14)

        pumps_col = QVBoxLayout()
        pumps_col.setSpacing(8)
        pumps_hdr = QLabel("PUMPS")
        pumps_hdr.setObjectName("sectionHeader")
        pumps_col.addWidget(pumps_hdr)

        self._rows: list[PumpRow] = []
        count = P.NUM_PUMPS if show_spares else P.ACTIVE_PUMPS
        rows_host = QWidget()
        rows_v = QVBoxLayout(rows_host)
        rows_v.setContentsMargins(0, 0, 0, 0)
        rows_v.setSpacing(8)
        for i in range(count):
            row = PumpRow(i)
            row.speedChanged.connect(self._on_speed)
            row.enableToggled.connect(self._on_enable)
            row.jogRequested.connect(self._on_jog)
            row.jogStopped.connect(self._on_jog_stop)
            self._rows.append(row)
            rows_v.addWidget(row)
        rows_v.addStretch(1)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(rows_host)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        pumps_col.addWidget(scroll, 1)
        mid.addLayout(pumps_col, 5)

        self._phases = PhasePanel()
        self._phases.setMinimumWidth(264)
        self._phases.setMaximumWidth(380)
        self._phases.phaseTimeChanged.connect(
            lambda p, s: self.link.send(P.cmd_phasetime(p, s))
        )
        mid.addWidget(self._phases, 4)
        root.addLayout(mid, 1)

        # --- action buttons -------------------------------------------
        self._run = _btn("RUN", "run", self._do_run)
        self._skip = _btn("SKIP", "skip", lambda: self.link.send(P.cmd_skip()))
        self._stop = _btn("STOP", "stop", lambda: self.link.send(P.cmd_stop()))
        self._estop = _btn("E-STOP", "estop", lambda: self.link.send(P.cmd_estop()))
        actions = QGridLayout()
        actions.setSpacing(10)
        for col, b in enumerate((self._run, self._skip, self._stop, self._estop)):
            actions.addWidget(b, 0, col)
        root.addLayout(actions)

        # --- log ------------------------------------------------------
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        self._log.setFixedHeight(92)
        root.addWidget(self._log)

        # --- wire link ----------------------------------------------
        link.telemetry.connect(self._on_telemetry)
        link.event.connect(lambda e: self._append("event", e))
        link.error.connect(lambda e: self._append("ERR", e))
        link.log.connect(lambda m: self._append("fw", m))
        link.connected.connect(self._on_connected)

        self._set_running(False)

    # ---- link -> UI ----------------------------------------------------
    def _on_connected(self, ok: bool) -> None:
        self._status.setText("connected" if ok else "disconnected")
        self._status.setProperty("connected", "true" if ok else "false")
        st = self._status.style()
        st.unpolish(self._status)
        st.polish(self._status)

    def _on_telemetry(self, t: P.Telemetry) -> None:
        self._last_telem = t
        for row in self._rows:
            row.apply_state(t.pumps[row.idx], t.running)
        self._phases.apply_telemetry(t)
        self._set_running(t.running)

    def _set_running(self, running: bool) -> None:
        self._run.setEnabled(not running)
        self._skip.setEnabled(running)

    # ---- UI -> link --------------------------------------------------
    def _do_run(self) -> None:
        self.link.send(P.cmd_run())

    def _on_speed(self, idx: int, steps: int) -> None:
        self.link.send(P.cmd_speed(idx, steps))

    def _on_enable(self, idx: int, on: bool) -> None:
        self.link.send(P.cmd_enable(idx, on))

    def _on_jog(self, idx: int, steps: int) -> None:
        self.link.send(P.cmd_jog(idx, steps if steps else 500))

    def _on_jog_stop(self, idx: int) -> None:
        self.link.send(P.cmd_jog(idx, 0))

    # ---- misc -------------------------------------------------------
    def _append(self, tag: str, msg: str) -> None:
        self._log.appendPlainText("[{}] {}".format(tag, msg))

    def closeEvent(self, ev) -> None:  # noqa: N802 (Qt override)
        self.link.stop()
        super().closeEvent(ev)


def _btn(text: str, name: str, slot) -> QPushButton:
    b = QPushButton(text)
    b.setObjectName(name)
    b.setMinimumHeight(56)
    b.clicked.connect(slot)
    return b
