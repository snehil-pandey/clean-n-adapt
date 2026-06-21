from __future__ import annotations

import sys
from dataclasses import dataclass

from . import __version__
from .actions import clean_mode, quick_clean, refresh_cache_index, refresh_windows_shell, run_boost
from .apps import installed_apps
from .cleaner import human_size
from .db import db_path, get_setting, history_rows, load_scan, set_setting
from .diagnostics import audit_downloads, health_score, top_folders
from .monitor import snapshot
from .startup import list_startup_entries
from .system import is_admin


try:
    from PySide6.QtCore import Qt, QThread, Signal
    from PySide6.QtGui import QAction, QFont
    from PySide6.QtWidgets import (
        QApplication,
        QCheckBox,
        QComboBox,
        QFrame,
        QGridLayout,
        QHBoxLayout,
        QLabel,
        QLineEdit,
        QListWidget,
        QMainWindow,
        QMessageBox,
        QPushButton,
        QProgressBar,
        QStackedWidget,
        QStyle,
        QSystemTrayIcon,
        QTextEdit,
        QVBoxLayout,
        QWidget,
    )
except ImportError:  # pragma: no cover - shown only on machines without Qt installed.
    QApplication = None


ACCENT = "#2563eb"
BG = "#f6f7fb"
PANEL = "#ffffff"
TEXT = "#111827"
MUTED = "#6b7280"
BORDER = "#dde3ee"
GOOD = "#16a34a"
WARN = "#d97706"


class Worker(QThread):
    done = Signal(str)

    def __init__(self, fn, *args, **kwargs):
        super().__init__()
        self.fn = fn
        self.args = args
        self.kwargs = kwargs

    def run(self) -> None:
        try:
            result = self.fn(*self.args, **self.kwargs)
            if isinstance(result, tuple):
                self.done.emit(str(result[0]))
            elif isinstance(result, list):
                self.done.emit("\n".join(str(item) for item in result))
            else:
                self.done.emit(str(result))
        except Exception as exc:
            self.done.emit(f"Error: {exc}")


@dataclass
class NavItem:
    label: str
    page: QWidget


class CleanNAdaptWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Clean-n-Adapt")
        self.setMinimumSize(1120, 720)
        self.workers: list[Worker] = []
        self.nav_buttons: list[QPushButton] = []
        self.stack = QStackedWidget()
        self.log = QTextEdit()
        self.log.setReadOnly(True)
        self.log.setPlaceholderText("Activity appears here.")
        self._build()
        self._build_tray()
        self.refresh_dashboard()

    def _build(self) -> None:
        root = QWidget()
        root_layout = QHBoxLayout(root)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)

        sidebar = QFrame()
        sidebar.setObjectName("sidebar")
        sidebar.setFixedWidth(230)
        side_layout = QVBoxLayout(sidebar)
        side_layout.setContentsMargins(18, 22, 18, 18)
        title = QLabel("Clean-n-Adapt")
        title.setObjectName("appTitle")
        subtitle = QLabel(f"v{__version__}")
        subtitle.setObjectName("muted")
        side_layout.addWidget(title)
        side_layout.addWidget(subtitle)
        side_layout.addSpacing(18)

        pages = [
            NavItem("Dashboard", self.dashboard_page()),
            NavItem("Clean", self.clean_page()),
            NavItem("Boost", self.boost_page()),
            NavItem("Apps", self.apps_page()),
            NavItem("Monitor", self.monitor_page()),
            NavItem("Settings", self.settings_page()),
        ]
        for index, item in enumerate(pages):
            self.stack.addWidget(item.page)
            button = QPushButton(item.label)
            button.setObjectName("navButton")
            button.setCheckable(True)
            button.clicked.connect(lambda _, i=index: self.select_page(i))
            self.nav_buttons.append(button)
            side_layout.addWidget(button)
        side_layout.addStretch()
        self.admin_badge = QLabel("Admin: checking")
        self.admin_badge.setObjectName("badge")
        side_layout.addWidget(self.admin_badge)

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(26, 24, 26, 24)
        content_layout.addWidget(self.stack, 1)
        content_layout.addWidget(self.log, 0)
        self.log.setFixedHeight(120)

        root_layout.addWidget(sidebar)
        root_layout.addWidget(content, 1)
        self.setCentralWidget(root)
        self.setStyleSheet(STYLE)
        self.select_page(0)

    def _build_tray(self) -> None:
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return
        self.tray = QSystemTrayIcon(self)
        self.tray.setIcon(self.style().standardIcon(QStyle.SP_ComputerIcon))
        self.tray.setToolTip("Clean-n-Adapt")
        menu = self.tray.contextMenu()
        if menu is None:
            from PySide6.QtWidgets import QMenu

            menu = QMenu()
        quick = QAction("Quick Clean Preview", self)
        quick.triggered.connect(lambda: self.run_task("Quick Clean Preview", quick_clean, True, False))
        refresh = QAction("Refresh Shell", self)
        refresh.triggered.connect(lambda: self.run_task("Refresh Shell", refresh_windows_shell))
        show = QAction("Open Dashboard", self)
        show.triggered.connect(self.showNormal)
        quit_action = QAction("Exit", self)
        quit_action.triggered.connect(QApplication.quit)
        menu.addAction(show)
        menu.addAction(quick)
        menu.addAction(refresh)
        menu.addSeparator()
        menu.addAction(quit_action)
        self.tray.setContextMenu(menu)
        self.tray.show()

    def select_page(self, index: int) -> None:
        self.stack.setCurrentIndex(index)
        for i, button in enumerate(self.nav_buttons):
            button.setChecked(i == index)
        if index == 0:
            self.refresh_dashboard()

    def card(self, title: str, value: str, note: str = "") -> QFrame:
        frame = QFrame()
        frame.setObjectName("card")
        layout = QVBoxLayout(frame)
        label = QLabel(title)
        label.setObjectName("muted")
        metric = QLabel(value)
        metric.setObjectName("metric")
        note_label = QLabel(note)
        note_label.setObjectName("muted")
        layout.addWidget(label)
        layout.addWidget(metric)
        layout.addWidget(note_label)
        return frame

    def dashboard_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        heading = QLabel("Dashboard")
        heading.setObjectName("heading")
        self.health_bar = QProgressBar()
        self.health_bar.setRange(0, 100)
        self.health_bar.setFormat("Health %p%")
        self.cards = QGridLayout()
        layout.addWidget(heading)
        layout.addWidget(self.health_bar)
        layout.addLayout(self.cards)
        actions = QHBoxLayout()
        for text, fn in [
            ("Quick Clean", lambda: self.run_task("Quick Clean Preview", quick_clean, True, False)),
            ("Refresh Index", lambda: self.run_task("Refresh Index", refresh_cache_index)),
            ("Refresh Shell", lambda: self.run_task("Refresh Shell", refresh_windows_shell)),
        ]:
            button = QPushButton(text)
            button.setObjectName("primaryButton")
            button.clicked.connect(fn)
            actions.addWidget(button)
        layout.addLayout(actions)
        layout.addStretch()
        return page

    def refresh_dashboard(self) -> None:
        snap = snapshot(max_age_hours=None)
        score = health_score()
        apps = installed_apps()
        startups = list_startup_entries()
        self.health_bar.setValue(score.total)
        self.admin_badge.setText(f"Admin: {'yes' if is_admin() else 'no'}")
        while self.cards.count():
            item = self.cards.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        data = [
            ("Junk Found", human_size(snap.indexed_bytes), "indexed cleanup"),
            ("Apps", str(len(apps)), "deduplicated inventory"),
            ("Startup", str(len(startups)), "startup entries"),
            ("Disk Free", human_size(snap.disk_free), "system drive"),
            ("Memory Free", human_size(snap.memory_free), "available RAM"),
            ("DB", str(db_path()), "state location"),
        ]
        for index, (title, value, note) in enumerate(data):
            self.cards.addWidget(self.card(title, value, note), index // 3, index % 3)

    def clean_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(self.page_title("Clean"))
        layout.addWidget(self.info("Review first, then choose what to remove. Quick Clean defaults to preview."))
        buttons = QGridLayout()
        for index, (label, mode) in enumerate([
            ("Safe Clean", "quick"),
            ("Browser Cache", "browser"),
            ("Developer Cache", "dev"),
            ("Gaming Cache", "gaming"),
            ("Windows Cache", "windows"),
            ("Full Preview", "full"),
        ]):
            button = QPushButton(label)
            button.setObjectName("tileButton")
            button.clicked.connect(lambda _, m=mode: self.run_clean_mode(m))
            buttons.addWidget(button, index // 3, index % 3)
        layout.addLayout(buttons)
        layout.addStretch()
        return page

    def boost_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(self.page_title("Boost"))
        grid = QGridLayout()
        actions = [("Flush DNS", "dns"), ("Store Reset", "store"), ("Disk Cleanup", "disk"), ("Power Plan", "power"), ("Run Safe Set", "all")]
        for index, (label, kind) in enumerate(actions):
            button = QPushButton(label)
            button.setObjectName("tileButton")
            button.clicked.connect(lambda _, k=kind: self.run_task(f"Boost {k}", run_boost, k))
            grid.addWidget(button, index // 3, index % 3)
        layout.addLayout(grid)
        layout.addStretch()
        return page

    def apps_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(self.page_title("Apps"))
        refresh = QPushButton("Refresh App Inventory")
        refresh.setObjectName("primaryButton")
        refresh.clicked.connect(self.load_apps)
        self.apps_list = QListWidget()
        layout.addWidget(refresh)
        layout.addWidget(self.apps_list, 1)
        return page

    def monitor_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(self.page_title("Monitor"))
        self.monitor_text = QTextEdit()
        self.monitor_text.setReadOnly(True)
        button = QPushButton("Refresh Monitor")
        button.setObjectName("primaryButton")
        button.clicked.connect(self.refresh_monitor)
        layout.addWidget(button)
        layout.addWidget(self.monitor_text, 1)
        return page

    def settings_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(self.page_title("Settings"))
        self.install_path = QLineEdit(get_setting("install_dir", "C:\\Program Files\\cleanNadapt"))
        self.download_path = QLineEdit(get_setting("download_path", ""))
        self.path_scope = QComboBox()
        self.path_scope.addItems(["Machine", "User"])
        self.path_scope.setCurrentText(get_setting("path_scope", "Machine"))
        self.desktop_shortcut = QCheckBox("Create Desktop shortcut on install")
        save = QPushButton("Save Settings")
        save.setObjectName("primaryButton")
        save.clicked.connect(self.save_settings)
        for label, widget in [("Install path", self.install_path), ("Download location", self.download_path), ("PATH scope", self.path_scope)]:
            layout.addWidget(QLabel(label))
            layout.addWidget(widget)
        layout.addWidget(self.desktop_shortcut)
        layout.addWidget(save)
        layout.addStretch()
        return page

    def page_title(self, text: str) -> QLabel:
        label = QLabel(text)
        label.setObjectName("heading")
        return label

    def info(self, text: str) -> QLabel:
        label = QLabel(text)
        label.setObjectName("muted")
        label.setWordWrap(True)
        return label

    def run_clean_mode(self, mode: str) -> None:
        self.run_task(f"Clean {mode}", clean_mode, mode, True, False)

    def run_task(self, label: str, fn, *args, **kwargs) -> None:
        self.log.append(f"> {label}")
        worker = Worker(fn, *args, **kwargs)
        worker.done.connect(lambda text: self.finish_task(label, text))
        self.workers.append(worker)
        worker.start()

    def finish_task(self, label: str, text: str) -> None:
        self.log.append(text)
        self.refresh_dashboard()

    def load_apps(self) -> None:
        self.apps_list.clear()
        for app in installed_apps():
            self.apps_list.addItem(f"{app.name} - {app.publisher or 'Unknown'} - {app.app_kind}")
        self.log.append("App inventory refreshed.")

    def refresh_monitor(self) -> None:
        snap = snapshot(max_age_hours=None)
        downloads = audit_downloads()
        folders = top_folders(downloads.path, limit=5, depth=1)
        lines = [
            f"Disk free: {human_size(snap.disk_free)}",
            f"Memory free: {human_size(snap.memory_free)}",
            f"Indexed cleanup: {human_size(snap.indexed_bytes)}",
            f"Downloads: {human_size(downloads.total_bytes)}",
            "Largest Downloads folders/files:",
        ]
        lines.extend(f"- {folder.path.name}: {human_size(folder.bytes_total)}" for folder in folders)
        self.monitor_text.setPlainText("\n".join(lines))

    def save_settings(self) -> None:
        set_setting("install_dir", self.install_path.text().strip())
        set_setting("download_path", self.download_path.text().strip())
        set_setting("path_scope", self.path_scope.currentText())
        set_setting("desktop_shortcut", "yes" if self.desktop_shortcut.isChecked() else "no")
        QMessageBox.information(self, "Saved", "Settings saved.")


STYLE = f"""
QMainWindow, QWidget {{ background: {BG}; color: {TEXT}; font-family: Segoe UI; font-size: 10pt; }}
#sidebar {{ background: #101827; }}
#appTitle {{ color: white; font-size: 20px; font-weight: 700; }}
#muted {{ color: {MUTED}; }}
#heading {{ color: {TEXT}; font-size: 24px; font-weight: 700; margin-bottom: 12px; }}
#metric {{ font-size: 24px; font-weight: 700; color: {TEXT}; }}
#badge {{ color: white; padding: 8px; background: #1f2937; border-radius: 6px; }}
#card {{ background: {PANEL}; border: 1px solid {BORDER}; border-radius: 8px; padding: 12px; }}
QTextEdit, QLineEdit, QListWidget, QComboBox {{ background: white; border: 1px solid {BORDER}; border-radius: 6px; padding: 8px; }}
QProgressBar {{ background: white; border: 1px solid {BORDER}; border-radius: 6px; text-align: center; height: 24px; }}
QProgressBar::chunk {{ background: {GOOD}; border-radius: 6px; }}
QPushButton {{ background: white; border: 1px solid {BORDER}; border-radius: 7px; padding: 10px; text-align: left; }}
QPushButton:hover {{ border-color: {ACCENT}; }}
QPushButton#primaryButton {{ background: {ACCENT}; color: white; border: 0; font-weight: 600; text-align: center; }}
QPushButton#tileButton {{ min-height: 76px; font-size: 14px; font-weight: 600; text-align: center; }}
QPushButton#navButton {{ color: #cbd5e1; background: transparent; border: 0; padding: 11px; text-align: left; }}
QPushButton#navButton:checked {{ color: white; background: {ACCENT}; border-radius: 7px; }}
"""


def launch() -> int:
    if QApplication is None:
        print("PySide6 is not installed. Install dependencies or use the release build.")
        return 1
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    font = QFont("Segoe UI", 10)
    app.setFont(font)
    window = CleanNAdaptWindow()
    window.show()
    return app.exec()
