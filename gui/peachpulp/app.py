"""PEACH PULP main window."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from . import protocol as P
from .link import LinkBase
from .widgets import PhasePanel, PumpRow

_BTN_CSS = """
QPushButton { font-size: 18px; font-weight: 700; padding: 14px; border-radius: 8px; }
QPushButton#run    { background:#2ecc71; color:white; }
QPushButton#skip   { background:#f39c12; color:white; }
QPushButton#stop   { background:#7f8c8d; color:white; }
QPushButton#estop  { background:#e74c3c; color:white; }
QPushButton:disabled { background:#bdc3c7; color:#ecf0f1; }
"""


class MainWindow(QMainWindow):
    def __init__(self, link: LinkBase, show_spares: bool = False) -> None:
        super().__init__()
        self.link = link
        self.setWindowTitle("PEACH PULP")
        self._last_telem: P.Telemetry | None = None

        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)

        # --- status bar row ---
        self._status = QLabel("○ disconnected")
        self._status.setStyleSheet("font-size: 14px;")
        top = QHBoxLayout()
        title = QLabel("PEACH PULP")
        title.setFont(QFont("", 20, QFont.Bold))
        top.addWidget(title)
        top.addStretch(1)
        top.addWidget(self._status)
        root.addLayout(top)

        # --- pumps (left) + phases (right) ---
        mid = QHBoxLayout()
        pumps_box = QVBoxLayout()
        pumps_box.addWidget(QLabel("<b>Pumps</b>"))
        self._rows: list[PumpRow] = []
        count = P.NUM_PUMPS if show_spares else P.ACTIVE_PUMPS
        for i in range(count):
            row = PumpRow(i)
            row.speedChanged.connect(self._on_speed)
            row.enableToggled.connect(self._on_enable)
            row.jogRequested.connect(self._on_jog)
            row.jogStopped.connect(self._on_jog_stop)
            self._rows.append(row)
            pumps_box.addWidget(row)
        pumps_box.addStretch(1)
        mid.addLayout(pumps_box, 3)

        self._phases = PhasePanel()
        self._phases.phaseTimeChanged.connect(
            lambda p, s: self.link.send(P.cmd_phasetime(p, s))
        )
        mid.addWidget(self._phases, 2)
        root.addLayout(mid, 1)

        # --- action buttons ---
        self.setStyleSheet(_BTN_CSS)
        self._run = _btn("RUN", "run", self._do_run)
        self._skip = _btn("SKIP", "skip", lambda: self.link.send(P.cmd_skip()))
        self._stop = _btn("STOP", "stop", lambda: self.link.send(P.cmd_stop()))
        self._estop = _btn("E-STOP", "estop", lambda: self.link.send(P.cmd_estop()))
        actions = QGridLayout()
        actions.addWidget(self._run, 0, 0)
        actions.addWidget(self._skip, 0, 1)
        actions.addWidget(self._stop, 0, 2)
        actions.addWidget(self._estop, 0, 3)
        root.addLayout(actions)

        # --- log ---
        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(500)
        self._log.setFixedHeight(120)
        self._log.setStyleSheet("font-family: monospace; font-size: 11px;")
        root.addWidget(self._log)

        # --- wire link ---
        link.telemetry.connect(self._on_telemetry)
        link.event.connect(lambda e: self._append("event", e))
        link.error.connect(lambda e: self._append("ERR", e))
        link.log.connect(lambda m: self._append("fw", m))
        link.connected.connect(self._on_connected)

        self._set_running(False)

    # ---- link -> UI ----------------------------------------------------
    def _on_connected(self, ok: bool) -> None:
        self._status.setText("● connected" if ok else "○ disconnected")
        self._status.setStyleSheet(
            "color:#2ecc71; font-size:14px;" if ok else "color:#e74c3c; font-size:14px;"
        )

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
    b.setMinimumHeight(64)
    b.clicked.connect(slot)
    return b
