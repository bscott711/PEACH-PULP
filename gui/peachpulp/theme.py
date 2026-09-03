"""Dark (and light) theme for the PEACH PULP GUI.

Fusion style + a tuned QPalette + one QSS sheet. Deliberately **no** third-party
theme package (qdarkstyle / qt-material / pyqtdarktheme): a hand-tuned sheet is
far more predictable on the Pi, where ARM wheels and PySide6 version drift make
those packages a maintenance risk. Everything here is stock Qt.

    from peachpulp.theme import apply_theme
    apply_theme(app)              # dark (default)
    apply_theme(app, dark=False)  # light
"""
from __future__ import annotations

from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication


class _Colors:
    """Flat colour set for one mode. Semantic button colours are shared."""

    def __init__(self, dark: bool) -> None:
        if dark:
            self.window = "#12151b"
            self.surface = "#1b1f27"
            self.surface_alt = "#232a35"
            self.border = "#2c3440"
            self.text = "#e7ebf2"
            self.text_muted = "#9aa4b2"
            self.accent = "#4aa3ff"
            self.accent_text = "#0b1220"
            self.disabled_bg = "#242a33"
            self.disabled_fg = "#6c7688"
        else:
            self.window = "#eef1f5"
            self.surface = "#ffffff"
            self.surface_alt = "#e6eaf0"
            self.border = "#d0d6de"
            self.text = "#1b2027"
            self.text_muted = "#5b6472"
            self.accent = "#2f6fd6"
            self.accent_text = "#ffffff"
            self.disabled_bg = "#e2e6ea"
            self.disabled_fg = "#a4acb8"
        # semantic — identical in both modes
        self.run = "#2ecc71"
        self.skip = "#f39c12"
        self.stop = "#5b6674"
        self.estop = "#e74c3c"
        self.ok = "#2ecc71"
        self.bad = "#e74c3c"


def _palette(c: _Colors) -> QPalette:
    p = QPalette()
    p.setColor(QPalette.Window, QColor(c.window))
    p.setColor(QPalette.WindowText, QColor(c.text))
    p.setColor(QPalette.Base, QColor(c.surface))
    p.setColor(QPalette.AlternateBase, QColor(c.surface_alt))
    p.setColor(QPalette.Text, QColor(c.text))
    p.setColor(QPalette.Button, QColor(c.surface_alt))
    p.setColor(QPalette.ButtonText, QColor(c.text))
    p.setColor(QPalette.ToolTipBase, QColor(c.surface_alt))
    p.setColor(QPalette.ToolTipText, QColor(c.text))
    p.setColor(QPalette.PlaceholderText, QColor(c.text_muted))
    p.setColor(QPalette.Highlight, QColor(c.accent))
    p.setColor(QPalette.HighlightedText, QColor(c.accent_text))
    p.setColor(QPalette.Link, QColor(c.accent))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        p.setColor(QPalette.Disabled, role, QColor(c.disabled_fg))
    return p


def _qss(c: _Colors) -> str:
    return f"""
    QWidget {{ color: {c.text}; font-size: 15px; }}
    QMainWindow, QWidget#central {{ background: {c.window}; }}
    QScrollArea, QScrollArea > QWidget > QWidget {{ background: transparent; }}

    QLabel#title {{ font-size: 24px; font-weight: 800; letter-spacing: 1px; }}
    QLabel#sectionHeader {{
        font-size: 11px; font-weight: 700; letter-spacing: 2px;
        color: {c.text_muted}; padding: 2px 0;
    }}
    QLabel#phaseHeadline {{ font-size: 17px; font-weight: 800; }}
    QLabel#phaseMark {{ color: {c.accent}; font-size: 14px; }}

    QLabel#statusPill {{
        padding: 4px 12px; border-radius: 11px; font-size: 13px; font-weight: 700;
        background: {c.surface_alt};
    }}
    QLabel#statusPill[connected="true"]  {{ color: {c.ok};  border: 1px solid {c.ok}; }}
    QLabel#statusPill[connected="false"] {{ color: {c.bad}; border: 1px solid {c.bad}; }}

    QFrame#card {{
        background: {c.surface}; border: 1px solid {c.border}; border-radius: 12px;
    }}
    QFrame#card[active="true"] {{ border: 2px solid {c.accent}; }}

    QTabWidget::pane {{
        border: 1px solid {c.border}; border-radius: 10px; top: -1px;
    }}
    QTabBar::tab {{
        background: {c.surface_alt}; color: {c.text_muted};
        padding: 10px 22px; margin-right: 4px; font-weight: 700;
        border-top-left-radius: 9px; border-top-right-radius: 9px;
    }}
    QTabBar::tab:selected {{ background: {c.surface}; color: {c.text};
        border: 1px solid {c.border}; border-bottom: none; }}
    QTabBar::tab:!selected:hover {{ color: {c.text}; }}

    QComboBox {{
        background: {c.surface_alt}; border: 1px solid {c.border};
        border-radius: 8px; padding: 6px 10px; min-height: 26px;
    }}
    QComboBox:focus {{ border: 1px solid {c.accent}; }}
    QComboBox:disabled {{ color: {c.disabled_fg}; background: {c.disabled_bg}; }}
    QComboBox QAbstractItemView {{
        background: {c.surface}; border: 1px solid {c.border};
        selection-background-color: {c.accent}; selection-color: {c.accent_text};
    }}

    QLineEdit {{
        background: {c.surface_alt}; border: 1px solid {c.border};
        border-radius: 8px; padding: 6px 10px; min-height: 24px;
        selection-background-color: {c.accent}; selection-color: {c.accent_text};
    }}
    QLineEdit:focus {{ border: 1px solid {c.accent}; }}
    QLineEdit:disabled {{ color: {c.disabled_fg}; background: {c.disabled_bg}; }}

    QSlider::groove:horizontal {{
        height: 6px; border-radius: 3px; background: {c.surface_alt};
    }}
    QSlider::sub-page:horizontal {{ background: {c.accent}; border-radius: 3px; }}
    QSlider::handle:horizontal {{
        background: {c.text}; width: 20px; height: 20px; margin: -8px 0;
        border-radius: 10px;
    }}
    QSlider::handle:horizontal:disabled {{ background: {c.disabled_fg}; }}
    QSlider:disabled {{ }}

    QPushButton#iconbtn {{
        background: transparent; border: 1px solid {c.border};
        border-radius: 8px; padding: 4px 10px; font-weight: 700; color: {c.text_muted};
    }}
    QPushButton#iconbtn:hover {{ color: {c.bad}; border-color: {c.bad}; }}
    QPushButton#ghostbtn {{
        background: transparent; border: 1px dashed {c.border};
        border-radius: 8px; padding: 8px 14px; color: {c.text_muted}; font-weight: 700;
    }}
    QPushButton#ghostbtn:hover {{ color: {c.accent}; border-color: {c.accent}; }}

    /* per-motor holding-torque toggle: Hold = normal, Free = amber (loose shaft) */
    QPushButton#holdtoggle {{ font-weight: 700; }}
    QPushButton#holdtoggle:checked {{
        background: {c.surface_alt}; color: {c.text}; border: 1px solid {c.border};
    }}
    QPushButton#holdtoggle:!checked {{
        background: {c.skip}; color: white; border: none;
    }}
    QPushButton#holdtoggle:disabled {{
        background: {c.disabled_bg}; color: {c.disabled_fg}; border-color: {c.border};
    }}

    QSpinBox {{
        background: {c.surface_alt}; border: 1px solid {c.border};
        border-radius: 8px; padding: 6px 8px;
        selection-background-color: {c.accent}; selection-color: {c.accent_text};
    }}
    QSpinBox:focus {{ border: 1px solid {c.accent}; }}
    QSpinBox:disabled {{ color: {c.disabled_fg}; background: {c.disabled_bg}; }}
    QSpinBox::up-button, QSpinBox::down-button {{
        width: 24px; border: none; background: transparent;
    }}
    QSpinBox::up-button:hover, QSpinBox::down-button:hover {{ background: {c.border}; }}
    QSpinBox::up-arrow, QSpinBox::down-arrow {{ width: 9px; height: 9px; }}

    QCheckBox {{ spacing: 8px; }}
    QCheckBox::indicator {{
        width: 22px; height: 22px; border-radius: 6px;
        border: 1px solid {c.border}; background: {c.surface_alt};
    }}
    QCheckBox::indicator:checked {{ background: {c.accent}; border-color: {c.accent}; }}
    QCheckBox:disabled {{ color: {c.disabled_fg}; }}

    QPushButton {{
        background: {c.surface_alt}; border: 1px solid {c.border};
        border-radius: 8px; padding: 8px 14px; font-weight: 600;
    }}
    QPushButton:hover {{ border-color: {c.accent}; }}
    QPushButton:pressed {{ background: {c.border}; }}
    QPushButton:checked {{
        background: {c.accent}; color: {c.accent_text}; border-color: {c.accent};
    }}
    QPushButton:disabled {{
        background: {c.disabled_bg}; color: {c.disabled_fg}; border-color: {c.border};
    }}

    QPushButton#run, QPushButton#skip, QPushButton#stop {{
        color: white; border: none; border-radius: 10px;
        padding: 16px; font-size: 18px; font-weight: 800; letter-spacing: 1px;
    }}
    QPushButton#run  {{ background: {c.run}; }}
    QPushButton#skip {{ background: {c.skip}; }}
    QPushButton#stop {{ background: {c.stop}; }}
    QPushButton#run:hover, QPushButton#skip:hover, QPushButton#stop:hover {{ border: none; }}
    QPushButton#run:disabled, QPushButton#skip:disabled, QPushButton#stop:disabled {{
        background: {c.disabled_bg}; color: {c.disabled_fg};
    }}

    QProgressBar {{
        background: {c.surface_alt}; border: 1px solid {c.border};
        border-radius: 8px; min-height: 24px; text-align: center; color: {c.text};
    }}
    QProgressBar::chunk {{ background: {c.accent}; border-radius: 7px; }}

    QPlainTextEdit {{
        background: {c.surface}; border: 1px solid {c.border}; border-radius: 8px;
        font-family: "DejaVu Sans Mono", "Courier New", monospace;
        font-size: 12px; color: {c.text_muted};
    }}

    QScrollBar:vertical {{ background: transparent; width: 10px; margin: 2px; }}
    QScrollBar::handle:vertical {{
        background: {c.border}; border-radius: 5px; min-height: 30px;
    }}
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{ height: 0; }}
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{ background: none; }}

    QToolTip {{
        background: {c.surface_alt}; color: {c.text}; border: 1px solid {c.border};
    }}
    """


def apply_theme(app: QApplication, dark: bool = True) -> None:
    """Style the whole application: Fusion + palette + QSS."""
    app.setStyle("Fusion")
    c = _Colors(dark)
    app.setPalette(_palette(c))
    app.setStyleSheet(_qss(c))
