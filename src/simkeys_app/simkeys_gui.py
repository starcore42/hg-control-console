import argparse
import json
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from tkinter.scrolledtext import ScrolledText

from . import simkeys_damage_meter as damage_meter
from . import simkeys_runtime as runtime
from .simkeys_script_host import (
    AutoAAScript,
    CoordinateFollowScript,
    DEFAULT_TIMER_OVERLAY_OFFSET_Y,
    OVERLAY_SCRIPT_CONTROLS,
    ScriptManager,
    WEAPON_BASE_SLOT_CHOICES,
    WEAPON_SLOT_NONE,
)


BASIC_FUNCTIONS_SCRIPT_ID = "always_on"
COORDINATE_FOLLOW_SCRIPT_ID = "coordinate_follow"
TIMERS_SCRIPT_ID = "ingame_timers"
DEFAULT_AUTO_START_SCRIPT_IDS = (BASIC_FUNCTIONS_SCRIPT_ID, TIMERS_SCRIPT_ID)
CLIENT_PANE_DEFAULT_WIDTH = 430
CLIENT_PANE_MIN_WIDTH = 360


def _probe_error_is_busy(text):
    if not text:
        return False
    lowered = str(text).lower()
    return (
        "err=5" in lowered
        or "err=32" in lowered
        or "err=231" in lowered
        or "access is denied" in lowered
        or "sharing violation" in lowered
        or "all pipe instances are busy" in lowered
        or "pipe busy" in lowered
    )


class Tooltip:
    def __init__(self, widget, text, delay_ms=350):
        self.widget = widget
        self.text = str(text or "").strip()
        self.delay_ms = int(delay_ms)
        self.after_id = None
        self.window = None
        if not self.text:
            return
        widget.bind("<Enter>", self.schedule, add="+")
        widget.bind("<Leave>", self.hide, add="+")
        widget.bind("<ButtonPress>", self.hide, add="+")
        widget.bind("<FocusIn>", self.schedule, add="+")
        widget.bind("<FocusOut>", self.hide, add="+")

    def schedule(self, _event=None):
        self.cancel()
        self.after_id = self.widget.after(self.delay_ms, self.show)

    def cancel(self):
        if self.after_id is not None:
            try:
                self.widget.after_cancel(self.after_id)
            except tk.TclError:
                pass
            self.after_id = None

    def show(self):
        self.after_id = None
        if self.window is not None or not self.text:
            return
        try:
            x = self.widget.winfo_rootx() + 18
            y = self.widget.winfo_rooty() + self.widget.winfo_height() + 8
            self.window = tk.Toplevel(self.widget)
            self.window.wm_overrideredirect(True)
            self.window.wm_geometry(f"+{x}+{y}")
            label = tk.Label(
                self.window,
                text=self.text,
                justify="left",
                background="#fff8dc",
                foreground="#212529",
                relief="solid",
                borderwidth=1,
                padx=8,
                pady=5,
                wraplength=320,
            )
            label.pack()
        except tk.TclError:
            self.window = None

    def hide(self, _event=None):
        self.cancel()
        if self.window is not None:
            try:
                self.window.destroy()
            except tk.TclError:
                pass
            self.window = None


SCRIPT_CARD_LAYOUTS = {
    "autodrink": {
        "expanded": False,
        "sections": [
            ("Quickbar", ["page", "slot"]),
            ("Trigger", ["threshold_percent", "cooldown_seconds"]),
            ("Behavior", ["lock_target", "resume_attack"]),
        ],
        "advanced": ["poll_interval", "max_lines", "echo_console", "include_backlog"],
    },
    "stop_hitting": {
        "expanded": False,
        "sections": [
            ("Potion", ["page", "slot", "cooldown_seconds"]),
        ],
        "advanced": ["poll_interval", "max_lines", "echo_console", "include_backlog"],
    },
    "auto_action": {
        "expanded": False,
        "sections": [
            ("Action", ["cooldown_seconds"]),
        ],
        "advanced": [],
    },
    "auto_attack": {
        "expanded": False,
        "sections": [
            ("Trigger", ["cooldown_seconds"]),
        ],
        "advanced": [],
    },
    "coordinate_follow": {
        "expanded": False,
        "sections": [
            ("Follow", ["follow_interval_seconds", "distance_threshold", "formation_radius", "bypass_no_walk", "combat_grace_seconds"]),
        ],
        "advanced_title": "Debug / Advanced",
        "advanced": ["position_poll_interval", "lead_stale_seconds", "poll_interval", "max_lines", "echo_console", "include_backlog"],
    },
    "always_on": {
        "expanded": False,
        "sections": [
            ("Assist", ["cooldown_seconds"]),
            ("Disable", ["disable_follow", "disable_wallet", "disable_spellbook_fill", "disable_fog_off"]),
        ],
        "advanced": ["follow_cues_dir", "poll_interval", "max_lines", "echo_console", "include_backlog"],
    },
    "auto_rsm": {
        "expanded": False,
        "sections": [
            ("Trigger", ["cooldown_seconds"]),
        ],
        "advanced": ["poll_interval", "max_lines", "echo_console", "include_backlog"],
    },
    "ingame_timers": {
        "expanded": False,
        "sections": [
            ("Overlay", ["position", "offset_x", "offset_y", "font_size", "color", "max_timers"]),
            ("Limbo", ["enable_limbo", "limbo_duration_seconds", "limbo_names"]),
        ],
        "advanced": ["rules_dir", "poll_interval", "max_lines", "include_backlog"],
    },
}
SCRIPT_CARD_ACCENTS = {
    "autodrink": "#2c7be5",
    "stop_hitting": "#e03131",
    "auto_aa": "#00a878",
    "auto_action": "#f59f00",
    "auto_attack": "#d9480f",
    "coordinate_follow": "#0b7285",
    "always_on": "#087f5b",
    "auto_rsm": "#7950f2",
    "ingame_timers": "#1971c2",
}
SCRIPT_ICON_LABELS = dict(OVERLAY_SCRIPT_CONTROLS)
BANK_PAGE_TO_VALUE = {"None": 0, "Shift": 1, "Control": 2}
BANK_VALUE_TO_PAGE = {value: label for label, value in BANK_PAGE_TO_VALUE.items()}
WEAPON_SLOT_RENDER_ORDER = [choice for choice in WEAPON_BASE_SLOT_CHOICES if choice != WEAPON_SLOT_NONE]
AUTO_DAMAGE_WEAPON_MODES = (AutoAAScript.MODE_WEAPON_SWAP, AutoAAScript.MODE_SHIFTER_WEAPON_SWAP)
SCRIPT_CONFIG_SOURCE_DEFAULT = "default"
SCRIPT_CONFIG_SOURCE_CHARACTER = "character"
SCRIPT_CONFIG_SOURCE_MANUAL = "manual"


def _weapon_choice_display(choice):
    text = str(choice or "").strip().upper()
    if text.startswith("S+F"):
        return f"Shift+F{text[3:]}"
    if text.startswith("C+F"):
        return f"Ctrl+F{text[3:]}"
    return text


def _weapon_mode_limit(mode):
    if str(mode or "").strip() not in AUTO_DAMAGE_WEAPON_MODES:
        return 0
    return int(AutoAAScript.MAX_WEAPON_BINDINGS)


class ScrollableFrame(ttk.Frame):
    def __init__(self, parent, stretch_height=False):
        super().__init__(parent)
        self.stretch_height = bool(stretch_height)
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)

        self.canvas = tk.Canvas(self, highlightthickness=0, borderwidth=0)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.interior = ttk.Frame(self.canvas)

        self.window_id = self.canvas.create_window((0, 0), window=self.interior, anchor="nw")
        self.canvas.configure(yscrollcommand=self.scrollbar.set)

        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.scrollbar.grid(row=0, column=1, sticky="ns")

        self.interior.bind("<Configure>", self._on_interior_configure)
        self.canvas.bind("<Configure>", self._on_canvas_configure)
        self.canvas.bind("<Enter>", self._bind_mousewheel)
        self.canvas.bind("<Leave>", self._unbind_mousewheel)
        self.interior.bind("<Enter>", self._bind_mousewheel)
        self.interior.bind("<Leave>", self._unbind_mousewheel)

    def _on_interior_configure(self, _event=None):
        try:
            self._sync_canvas_window()
        except tk.TclError:
            pass

    def _on_canvas_configure(self, event):
        try:
            self._sync_canvas_window(width=event.width, height=event.height)
        except tk.TclError:
            pass

    def _sync_canvas_window(self, width=None, height=None):
        if not self.winfo_exists() or not self.canvas.winfo_exists():
            return
        if width is None:
            width = self.canvas.winfo_width()
        options = {"width": max(int(width), 1)}
        if self.stretch_height:
            if height is None:
                height = self.canvas.winfo_height()
            options["height"] = max(int(height), int(self.interior.winfo_reqheight()), 1)
        self.canvas.itemconfigure(self.window_id, **options)
        self.canvas.configure(scrollregion=self.canvas.bbox("all"))

    def refresh(self):
        try:
            self._sync_canvas_window()
        except tk.TclError:
            pass

    def _bind_mousewheel(self, _event=None):
        try:
            self.canvas.bind_all("<MouseWheel>", self._on_mousewheel)
        except tk.TclError:
            pass

    def _unbind_mousewheel(self, _event=None):
        try:
            self.canvas.unbind_all("<MouseWheel>")
        except tk.TclError:
            pass

    def _on_mousewheel(self, event):
        delta = int(-1 * (event.delta / 120))
        if delta:
            try:
                self.canvas.yview_scroll(delta, "units")
            except tk.TclError:
                pass


class ScriptCard:
    def __init__(self, parent, definition, app):
        self.app = app
        self.definition = definition
        self.fields_by_key = {field.key: field for field in definition.fields}
        self.vars = {}
        self.widget_holders = {}
        self.extra_controls = []
        self.wrap_targets = []
        self.expanded = bool(SCRIPT_CARD_LAYOUTS.get(definition.script_id, {}).get("expanded", False))
        self.advanced_expanded = False
        self.loaded_client_pid = None
        self.loading_config = False
        self.config_apply_after_id = None

        self.frame = ttk.Frame(parent, padding=(0, 8))
        self.frame.columnconfigure(1, weight=1)

        accent = tk.Frame(self.frame, width=5, background=SCRIPT_CARD_ACCENTS.get(definition.script_id, "#868e96"))
        accent.grid(row=0, column=0, rowspan=3, sticky="ns", padx=(0, 8))

        header = ttk.Frame(self.frame)
        header.grid(row=0, column=1, sticky="ew")

        accent_color = SCRIPT_CARD_ACCENTS.get(definition.script_id, "#868e96")
        self.icon_label = tk.Label(
            header,
            text=SCRIPT_ICON_LABELS.get(definition.script_id, "Sk"),
            width=3,
            background=accent_color,
            foreground="white",
            font=("Segoe UI", 9, "bold"),
            padx=4,
            pady=2,
        )
        self.icon_label.grid(row=0, column=0, sticky="w", padx=(0, 8))

        self.name_label = ttk.Label(header, text=definition.name)
        self.name_label.grid(row=0, column=1, sticky="w")

        self.auto_start_var = tk.BooleanVar(value=False)
        self.auto_start_check = ttk.Checkbutton(
            header,
            text="Saved",
            variable=self.auto_start_var,
            command=self.on_auto_start_changed,
        )
        self.auto_start_check.grid(row=0, column=2, padx=(12, 0), sticky="w")

        status_column = 3
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            self.coordinate_saved_lead_var = tk.BooleanVar(value=False)
            self.coordinate_saved_lead_check = ttk.Checkbutton(
                header,
                text="Lead",
                variable=self.coordinate_saved_lead_var,
                command=self.on_coordinate_saved_lead_changed,
            )
            self.coordinate_saved_lead_check.grid(row=0, column=3, padx=(8, 0), sticky="w")
            self.extra_controls.append(("bool", self.coordinate_saved_lead_check))
            status_column = 4

        header.columnconfigure(status_column, weight=1)
        self.status_var = tk.StringVar(value="Stopped")
        self.status_label = ttk.Label(header, textvariable=self.status_var)
        self.status_label.grid(row=0, column=status_column, padx=(12, 10), sticky="w")

        next_column = status_column + 1
        if self.definition.script_id == "auto_aa":
            self._create_header_mode_control(header, next_column, on_change=self.on_auto_damage_mode_changed)
            next_column += 2
        elif self.definition.script_id in ("auto_action", "auto_rsm"):
            self._create_header_mode_control(header, next_column)
            next_column += 2

        if self.definition.script_id == "auto_attack":
            self.lead_button = ttk.Button(
                header,
                text="Set Selected as Lead",
                command=self.on_assign_lead,
                width=20,
            )
            self.lead_button.grid(row=0, column=next_column, padx=(0, 8), sticky="e")
            self.extra_controls.append(("button", self.lead_button))
            next_column += 1

        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            self.coordinate_lead_button = ttk.Button(
                header,
                text="Start Lead",
                command=self.on_start_coordinate_lead,
                width=12,
            )
            self.coordinate_lead_button.grid(row=0, column=next_column, padx=(0, 6), sticky="e")
            self.extra_controls.append(("button", self.coordinate_lead_button))
            next_column += 1

            self.coordinate_follower_button = ttk.Button(
                header,
                text="Start Follower",
                command=self.on_start_coordinate_follower,
                width=14,
            )
            self.coordinate_follower_button.grid(row=0, column=next_column, padx=(0, 8), sticky="e")
            self.extra_controls.append(("button", self.coordinate_follower_button))
            next_column += 1

        self.expand_button = ttk.Button(header, text="", command=self.on_expand_toggle, width=12)
        self.expand_button.grid(row=0, column=next_column, padx=(0, 8), sticky="e")
        next_column += 1

        self.toggle_button = ttk.Button(header, text=self._toggle_button_text(False), command=self.on_toggle, width=18)
        self.toggle_button.grid(row=0, column=next_column, sticky="e")

        self.body = ttk.Frame(self.frame, padding=(16, 8, 0, 0))
        self.body.columnconfigure(0, weight=1)
        self.body.grid(row=1, column=1, sticky="ew")

        description_text = str(getattr(definition, "details", "") or definition.description or "").strip()
        self.description_label = ttk.Label(
            self.body,
            text=description_text,
            justify="left",
            wraplength=560,
        )
        self.description_label.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        self.wrap_targets.append((self.description_label, 36))

        self.content = ttk.Frame(self.body)
        self.content.grid(row=1, column=0, sticky="ew")
        self.content.columnconfigure(0, weight=1)

        if self.definition.script_id == "auto_aa":
            self._build_auto_damage_content()
        else:
            self._build_generic_content()

        self.separator = ttk.Separator(self.frame, orient="horizontal")
        self.separator.grid(row=2, column=1, sticky="ew", pady=(8, 0))
        self.frame.bind("<Configure>", self._on_card_resize)
        self._apply_expanded_state()

    def _toggle_button_text(self, running: bool) -> str:
        if self.definition.script_id == "stop_hitting":
            return "Stop Guard" if running else "Start Guard"
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            return "Stop Coordinate Follow" if running else "Stopped"
        action = "Stop" if running else "Start"
        return f"{action} {self.definition.name}"

    def _create_header_mode_control(self, parent, column, on_change=None):
        field = self.fields_by_key["mode"]
        ttk.Label(parent, text=f"{field.label}:").grid(row=0, column=column, padx=(0, 4), sticky="e")
        var = tk.StringVar(value=str(field.default))
        self._trace_config_var(var)
        widget = ttk.Combobox(
            parent,
            textvariable=var,
            values=list(field.choices or []),
            width=field.width,
            state="readonly",
        )
        widget.grid(row=0, column=column + 1, padx=(0, 8), sticky="e")
        if on_change is not None:
            widget.bind("<<ComboboxSelected>>", on_change)
        self.vars[field.key] = (field, var, widget)

    def _build_generic_content(self):
        layout = SCRIPT_CARD_LAYOUTS.get(self.definition.script_id, {"sections": [], "advanced": []})
        row = 0
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            self.coordinate_runtime_var = tk.StringVar(value="Current coordinates: <unknown>")
            runtime_frame = ttk.LabelFrame(self.content, text="Coordinates", padding=8)
            runtime_frame.grid(row=row, column=0, sticky="ew", pady=(0, 8))
            runtime_frame.columnconfigure(0, weight=1)
            runtime_label = ttk.Label(runtime_frame, textvariable=self.coordinate_runtime_var, justify="left", wraplength=520)
            runtime_label.grid(row=0, column=0, sticky="ew")
            self.wrap_targets.append((runtime_label, 48))
            row += 1

        for title, field_keys in layout.get("sections", []):
            section = ttk.LabelFrame(self.content, text=title, padding=8)
            section.grid(row=row, column=0, sticky="ew", pady=(0, 8))
            self._build_field_grid(section, field_keys, columns=min(max(len(field_keys), 1), 2))
            row += 1

        advanced_keys = layout.get("advanced", [])
        if advanced_keys:
            advanced_title = layout.get("advanced_title", "Advanced")
            self.advanced_toggle_var = tk.StringVar(value="Show Advanced")
            ttk.Button(
                self.content,
                textvariable=self.advanced_toggle_var,
                command=self.on_advanced_toggle,
                width=14,
            ).grid(row=row, column=0, sticky="w")
            row += 1

            self.advanced_body = ttk.LabelFrame(self.content, text=advanced_title, padding=8)
            self.advanced_body.grid(row=row, column=0, sticky="ew", pady=(8, 0))
            self._build_field_grid(self.advanced_body, advanced_keys, columns=min(max(len(advanced_keys), 1), 2))
            if not self.advanced_expanded:
                self.advanced_body.grid_remove()

    def _build_auto_damage_content(self):
        self.mode_hint_var = tk.StringVar(value="")
        self.mode_hint_label = ttk.Label(self.content, textvariable=self.mode_hint_var, justify="left", wraplength=520)
        self.mode_hint_label.grid(
            row=0,
            column=0,
            sticky="ew",
            pady=(0, 8),
        )
        self.wrap_targets.append((self.mode_hint_label, 36))

        self.command_section = ttk.LabelFrame(self.content, text="Command Switching", padding=8)
        self.command_section.grid(row=1, column=0, sticky="ew", pady=(0, 8))
        self._create_field_holder(self.command_section, "elemental_dice", row=0, column=0)
        self._create_field_holder(self.command_section, "auto_canister", row=0, column=1)
        self._create_field_holder(self.command_section, "canister_cooldown_seconds", row=1, column=0)

        self.weapon_section = ttk.LabelFrame(self.content, text="Weapon Swapping", padding=8)
        self.weapon_section.grid(row=2, column=0, sticky="ew", pady=(0, 8))
        self.weapon_section.columnconfigure(0, weight=1)

        weapon_top = ttk.Frame(self.weapon_section)
        weapon_top.grid(row=0, column=0, sticky="ew")

        self._create_field_holder(weapon_top, "swap_cooldown_seconds", row=0, column=0)
        self._create_field_holder(weapon_top, "min_swap_gain_percent", row=0, column=1)
        self._create_field_holder(weapon_top, "shift_slot", row=0, column=2)
        self._create_field_holder(weapon_top, "shifter_min_swap_gain_percent", row=1, column=1)
        self._create_field_holder(weapon_top, "shifter_healing_only", row=1, column=2)

        self.weapon_limit_var = tk.StringVar(value="")
        self.weapon_limit_label = ttk.Label(self.weapon_section, textvariable=self.weapon_limit_var, justify="left", wraplength=520)
        self.weapon_limit_label.grid(
            row=1,
            column=0,
            sticky="ew",
            pady=(0, 8),
        )
        self.wrap_targets.append((self.weapon_limit_label, 48))

        self.weapon_summary_var = tk.StringVar(value="Selected: none")
        self.weapon_summary_label = ttk.Label(self.weapon_section, textvariable=self.weapon_summary_var, justify="left", wraplength=520)
        self.weapon_summary_label.grid(
            row=2,
            column=0,
            sticky="ew",
            pady=(0, 8),
        )
        self.wrap_targets.append((self.weapon_summary_label, 48))

        self.weapon_learning_var = tk.StringVar(value="Learned weapons: start Weapon Swap to populate this.")
        self.weapon_learning_label = ttk.Label(self.weapon_section, textvariable=self.weapon_learning_var, justify="left", wraplength=520)
        self.weapon_learning_label.grid(
            row=3,
            column=0,
            sticky="ew",
            pady=(0, 8),
        )
        self.wrap_targets.append((self.weapon_learning_label, 48))

        self.weapon_slot_hint_label = ttk.Label(
            self.weapon_section,
            text="Tick the base quickbar slots that contain weapons you want Auto Damage to use. Shift/Ctrl weapon slots are not used because NWN's equipped-slot mask is unreliable there.",
            justify="left",
            wraplength=520,
        )
        self.weapon_slot_hint_label.grid(
            row=4,
            column=0,
            sticky="ew",
            pady=(0, 4),
        )
        self.wrap_targets.append((self.weapon_slot_hint_label, 48))

        grid = ttk.Frame(self.weapon_section)
        grid.grid(row=5, column=0, sticky="w")
        ttk.Label(grid, text="").grid(row=0, column=0, padx=(0, 8))
        for slot in range(1, 13):
            ttk.Label(grid, text=str(slot), width=4, anchor="center").grid(row=0, column=slot, padx=1, pady=(0, 2))

        self.weapon_slot_vars = {}
        ttk.Label(grid, text="Base", width=7).grid(row=1, column=0, padx=(0, 8), sticky="w")
        for slot in range(1, 13):
            choice = f"F{slot}"
            var = tk.BooleanVar(value=False)
            self._trace_config_var(var)
            widget = ttk.Checkbutton(grid, variable=var, command=self.on_weapon_slots_changed)
            widget.grid(row=1, column=slot, padx=1, pady=1, sticky="w")
            self.weapon_slot_vars[choice] = (var, widget)
            self.extra_controls.append(("bool", widget))

        self.advanced_toggle_var = tk.StringVar(value="Show Advanced")
        ttk.Button(
            self.content,
            textvariable=self.advanced_toggle_var,
            command=self.on_advanced_toggle,
            width=14,
        ).grid(row=3, column=0, sticky="w")

        self.advanced_body = ttk.LabelFrame(self.content, text="Advanced", padding=8)
        self.advanced_body.grid(row=4, column=0, sticky="ew", pady=(8, 0))
        self._build_field_grid(self.advanced_body, ["poll_interval", "max_lines", "echo_console", "include_backlog"], columns=2)
        if not self.advanced_expanded:
            self.advanced_body.grid_remove()

        self.on_auto_damage_mode_changed()

    def _build_field_grid(self, parent, field_keys, columns=3):
        for index, field_key in enumerate(field_keys):
            if field_key not in self.fields_by_key:
                continue
            row = index // max(columns, 1)
            column = index % max(columns, 1)
            self._create_field_holder(parent, field_key, row=row, column=column)

    def _create_field_holder(self, parent, field_key, row, column):
        field = self.fields_by_key[field_key]
        holder = ttk.Frame(parent)
        holder.grid(row=row, column=column, sticky="nw", padx=(0, 14), pady=(0, 8))
        label_row = ttk.Frame(holder)
        label_row.grid(row=0, column=0, sticky="w")
        ttk.Label(label_row, text=field.label).grid(row=0, column=0, sticky="w")
        help_text = str(getattr(field, "help_text", "") or "").strip()
        if help_text:
            help_label = tk.Label(
                label_row,
                text="?",
                width=2,
                cursor="question_arrow",
                foreground="#0b7285",
                font=("Segoe UI", 8, "bold"),
            )
            help_label.grid(row=0, column=1, padx=(4, 0), sticky="w")
            Tooltip(help_label, help_text)

        if field.kind == "bool":
            var = tk.BooleanVar(value=bool(field.default))
            self._trace_config_var(var)
            widget = ttk.Checkbutton(holder, variable=var)
        elif field.kind == "choice":
            var = tk.StringVar(value=str(field.default))
            self._trace_config_var(var)
            widget = ttk.Combobox(
                holder,
                textvariable=var,
                values=list(field.choices or []),
                width=field.width,
                state="readonly",
            )
        else:
            var = tk.StringVar(value=str(field.default))
            self._trace_config_var(var)
            widget = ttk.Entry(holder, textvariable=var, width=field.width)

        widget.grid(row=1, column=0, sticky="w")
        self.vars[field.key] = (field, var, widget)
        self.widget_holders[field.key] = holder
        return holder

    def _on_card_resize(self, event):
        width = max(int(event.width) - 40, 260)
        for widget, padding in self.wrap_targets:
            widget.configure(wraplength=max(width - padding, 220))

    def _trace_config_var(self, var):
        var.trace_add("write", self.on_config_value_changed)

    def on_config_value_changed(self, *_args):
        if self.loading_config or self.loaded_client_pid is None:
            return
        if self.config_apply_after_id is not None:
            try:
                self.frame.after_cancel(self.config_apply_after_id)
            except tk.TclError:
                pass
        self.config_apply_after_id = self.frame.after(450, self.apply_pending_config_change)

    def apply_pending_config_change(self):
        self.config_apply_after_id = None
        if self.loading_config or self.loaded_client_pid is None:
            return
        try:
            config = self.parse_config(validate_for_start=False)
        except Exception as exc:
            self.status_var.set(f"Invalid setting: {exc}")
            return
        self.app.apply_script_config_change(self.loaded_client_pid, self.definition.script_id, config)

    def grid(self, **kwargs):
        self.frame.grid(**kwargs)

    def load_for_client(self, client_pid):
        self.loaded_client_pid = client_pid
        self.loading_config = True
        try:
            self.auto_start_var.set(self.app.get_script_autostart(client_pid, self.definition.script_id))
            config = self.app.get_script_config(client_pid, self.definition.script_id)
            if self.definition.script_id == "auto_aa":
                self._load_auto_damage_config(config)
            else:
                self._load_standard_config(config)
        finally:
            self.loading_config = False
        self.refresh_state()

    def try_persist_for_client(self, client_pid):
        if client_pid is None or self.loaded_client_pid != client_pid:
            return False
        try:
            config = self.parse_config(validate_for_start=False)
        except Exception:
            return False
        current = self.app.get_script_config(client_pid, self.definition.script_id)
        changed = False
        if config != current:
            self.app.set_script_config(client_pid, self.definition.script_id, config)
            changed = True
        desired_auto_start = bool(self.auto_start_var.get())
        if desired_auto_start != self.app.get_script_autostart(client_pid, self.definition.script_id):
            self.app.set_script_autostart(client_pid, self.definition.script_id, desired_auto_start)
            changed = True
        return changed

    def _load_standard_config(self, config):
        for field, var, _widget in self.vars.values():
            value = config.get(field.key, field.default)
            if self.definition.script_id in ("autodrink", "stop_hitting") and field.key == "page":
                if isinstance(value, str) and value.strip() in BANK_PAGE_TO_VALUE:
                    var.set(value.strip())
                else:
                    var.set(BANK_VALUE_TO_PAGE.get(int(value or 0), "None"))
                continue
            if field.kind == "bool":
                var.set(bool(value))
            else:
                var.set(str(value))
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID and hasattr(self, "coordinate_saved_lead_var"):
            self.coordinate_saved_lead_var.set(self.app._coordinate_follow_config_is_lead(config))

    def _load_auto_damage_config(self, config):
        for key in (
            "mode",
            "elemental_dice",
            "auto_canister",
            "canister_cooldown_seconds",
            "swap_cooldown_seconds",
            "min_swap_gain_percent",
            "shift_slot",
            "shifter_min_swap_gain_percent",
            "shifter_healing_only",
            "poll_interval",
            "max_lines",
            "echo_console",
            "include_backlog",
        ):
            field, var, _widget = self.vars[key]
            value = config.get(field.key, field.default)
            if field.kind == "bool":
                var.set(bool(value))
            else:
                var.set(str(value))

        for var, _widget in self.weapon_slot_vars.values():
            var.set(False)

        selected_choices = []
        for index in range(1, 7):
            choice = str(config.get(f"weapon_slot_{index}", WEAPON_SLOT_NONE)).strip() or WEAPON_SLOT_NONE
            if choice != WEAPON_SLOT_NONE and choice in self.weapon_slot_vars:
                selected_choices.append(choice)
                self.weapon_slot_vars[choice][0].set(True)

        self.on_auto_damage_mode_changed()

    def set_enabled(self, enabled):
        button_state = "normal" if enabled else "disabled"
        for field, _var, widget in self.vars.values():
            self._set_widget_state(widget, field.kind, enabled)
        for control_kind, widget in self.extra_controls:
            self._set_widget_state(widget, control_kind, enabled)
        self.auto_start_check.configure(state=button_state)
        self.toggle_button.configure(state=button_state)
        if not enabled:
            self.status_var.set("Unavailable")
            self.toggle_button.configure(text=self._toggle_button_text(False))

    def _set_widget_state(self, widget, kind, enabled):
        if kind == "choice":
            state = "readonly" if enabled else "disabled"
        else:
            state = "normal" if enabled else "disabled"
        widget.configure(state=state)

    def parse_config(self, validate_for_start=True):
        if self.definition.script_id == "auto_aa":
            return self._parse_auto_damage_config(validate_for_start=validate_for_start)

        config = {}
        for field, var, _widget in self.vars.values():
            value = var.get()
            if field.kind == "bool":
                config[field.key] = bool(value)
            elif field.kind == "choice":
                text_value = str(value).strip()
                if field.choices and text_value not in field.choices:
                    raise RuntimeError(f"{field.label} must be one of: {', '.join(field.choices)}.")
                config[field.key] = text_value
            elif field.kind == "int":
                parsed = int(value)
                if field.minimum is not None and parsed < field.minimum:
                    raise RuntimeError(f"{field.label} must be at least {int(field.minimum)}.")
                if field.maximum is not None and parsed > field.maximum:
                    raise RuntimeError(f"{field.label} must be at most {int(field.maximum)}.")
                config[field.key] = parsed
            elif field.kind == "float":
                parsed = float(value)
                if field.minimum is not None and parsed < field.minimum:
                    raise RuntimeError(f"{field.label} must be at least {field.minimum}.")
                if field.maximum is not None and parsed > field.maximum:
                    raise RuntimeError(f"{field.label} must be at most {field.maximum}.")
                config[field.key] = parsed
            else:
                config[field.key] = str(value)
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            lead_var = getattr(self, "coordinate_saved_lead_var", None)
            saved_as_lead = bool(lead_var.get()) if lead_var is not None else False
            config["role"] = CoordinateFollowScript.ROLE_LEAD if saved_as_lead else CoordinateFollowScript.ROLE_FOLLOWER
        return config

    def _parse_auto_damage_config(self, validate_for_start=True):
        config = {}
        for key in (
            "mode",
            "elemental_dice",
            "auto_canister",
            "canister_cooldown_seconds",
            "swap_cooldown_seconds",
            "min_swap_gain_percent",
            "shift_slot",
            "shifter_min_swap_gain_percent",
            "shifter_healing_only",
            "poll_interval",
            "max_lines",
            "echo_console",
            "include_backlog",
        ):
            field, var, _widget = self.vars[key]
            value = var.get()
            if field.kind == "bool":
                config[field.key] = bool(value)
            elif field.kind == "choice":
                text_value = str(value).strip()
                if field.choices and text_value not in field.choices:
                    raise RuntimeError(f"{field.label} must be one of: {', '.join(field.choices)}.")
                config[field.key] = text_value
            elif field.kind == "int":
                parsed = int(value)
                if field.minimum is not None and parsed < field.minimum:
                    raise RuntimeError(f"{field.label} must be at least {int(field.minimum)}.")
                if field.maximum is not None and parsed > field.maximum:
                    raise RuntimeError(f"{field.label} must be at most {int(field.maximum)}.")
                config[field.key] = parsed
            elif field.kind == "float":
                parsed = float(value)
                if field.minimum is not None and parsed < field.minimum:
                    raise RuntimeError(f"{field.label} must be at least {field.minimum}.")
                if field.maximum is not None and parsed > field.maximum:
                    raise RuntimeError(f"{field.label} must be at most {field.maximum}.")
                config[field.key] = parsed
            else:
                config[field.key] = str(value)

        selected = self._selected_weapon_choices()
        for index in range(1, 7):
            config[f"weapon_slot_{index}"] = selected[index - 1] if index <= len(selected) else WEAPON_SLOT_NONE

        mode = str(config.get("mode", "")).strip()
        if validate_for_start and mode in AUTO_DAMAGE_WEAPON_MODES:
            max_bindings = _weapon_mode_limit(mode)
            if not selected:
                raise RuntimeError(
                    "Weapon Swap needs at least one weapon quickbar button selected. "
                    "Open Show Settings and tick the quickbar slots that contain weapons."
                )
            if len(selected) > max_bindings:
                raise RuntimeError(f"{mode} supports at most {max_bindings} weapon quickbar buttons.")
            if mode == AutoAAScript.MODE_SHIFTER_WEAPON_SWAP and config.get("shift_slot") == WEAPON_SLOT_NONE:
                raise RuntimeError("Shifter Weapon Swap needs the quickbar slot for your shift ability.")
        return config

    def refresh_state(self):
        client = self.app.selected_client()
        if client is None or not client.injected:
            self.set_enabled(False)
            return

        self.set_enabled(True)
        state = self.app.script_manager.get_state(client.pid, self.definition.script_id)
        busy_label = self.app.script_toggles_in_progress.get((client.pid, self.definition.script_id))
        if busy_label:
            self.status_var.set(busy_label)
        else:
            self.status_var.set(state["status"])
        self.toggle_button.configure(text=self._toggle_button_text(state["running"]))
        if busy_label:
            self.toggle_button.configure(state="disabled")
        if self.definition.script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            self._refresh_coordinate_follow_buttons(state, busy_label)
            self._refresh_coordinate_follow_runtime_details(state)
        if self.definition.script_id == "auto_aa":
            self._refresh_auto_damage_runtime_details(state.get("details", {}), state["running"])

    def _refresh_coordinate_follow_buttons(self, state, busy_label):
        if not hasattr(self, "coordinate_lead_button"):
            return
        busy = bool(busy_label)
        running = bool(state.get("running"))
        start_state = "disabled" if busy else "normal"
        stop_state = "disabled" if busy or not running else "normal"
        self.coordinate_lead_button.configure(state=start_state, text="Restart Lead" if running else "Start Lead")
        self.coordinate_follower_button.configure(state=start_state, text="Restart Follower" if running else "Start Follower")
        self.toggle_button.configure(state=stop_state)
        if self.loaded_client_pid is not None and hasattr(self, "coordinate_saved_lead_var"):
            config = self.app.get_script_config(self.loaded_client_pid, COORDINATE_FOLLOW_SCRIPT_ID)
            self.coordinate_saved_lead_var.set(self.app._coordinate_follow_config_is_lead(config))

    def _refresh_coordinate_follow_runtime_details(self, state):
        if not hasattr(self, "coordinate_runtime_var"):
            return
        client = self.app.selected_client()
        if client is None:
            self.coordinate_runtime_var.set("Current coordinates: <unknown>")
            return
        if getattr(client, "position_valid", False):
            current = f"{client.position_x:.2f}, {client.position_y:.2f}, {client.position_z:.2f}"
        else:
            current = "<unknown>"
        details = state.get("details") or {}
        saved_role = ""
        if self.loaded_client_pid is not None:
            saved_config = self.app.get_script_config(self.loaded_client_pid, COORDINATE_FOLLOW_SCRIPT_ID)
            saved_role = str(saved_config.get("role") or "").strip()
        runtime_role = str(details.get("role") or "").strip()
        lines = [
            f"Current coordinates: {current}",
            f"Saved role: {saved_role or 'Follower'}",
        ]
        if state.get("running"):
            lines.append(f"Runtime role: {runtime_role or 'Follower'}")
            lead_name = str(details.get("last_lead_name") or "").strip() or "<waiting>"
            lines.append(f"Lead: {lead_name} at {details.get('last_lead_position') or '<unknown>'}")
            lines.append(f"Move target: {details.get('last_target_position') or '<unknown>'}")
            lines.append(f"Bypass active: {'Yes' if details.get('walk_bypass_active') else 'No'}")
        self.coordinate_runtime_var.set("\n".join(lines))

    def _refresh_auto_damage_runtime_details(self, details, running):
        if not hasattr(self, "weapon_learning_var"):
            return
        if not running or not details.get("weapon_mode"):
            self.weapon_learning_var.set("Learned weapons: start Weapon Swap to populate this.")
            return

        weapons = list(details.get("weapons", []))
        if not weapons:
            self.weapon_learning_var.set("Learned weapons: waiting for configured weapon slots.")
            return

        lines = []
        current_display = details.get("current_display") or details.get("current_weapon") or "Unknown"
        pending_display = details.get("pending_display") or ""
        state_line = f"Current state: {current_display}"
        if pending_display:
            state_line += f", pending {pending_display}"
        unarmed_count = int(details.get("unarmed_observations") or 0)
        if unarmed_count:
            state_line += f", unarmed seen {unarmed_count}"
        if details.get("pending_conceal_seen"):
            state_line += ", round boundary seen"
        ignored_damage = int(details.get("pending_ignored_damage") or 0)
        if ignored_damage:
            state_line += f", ignored pre-boundary {ignored_damage}"
        equipped_display = str(details.get("equipped_display") or "").strip()
        if equipped_display:
            state_line += f", hook equipped {equipped_display}"
        if details.get("shifter_mode"):
            shifter_state = details.get("shifter_state") or "unknown"
            shifter_slot = details.get("shifter_shift_slot") or "-"
            state_line += f", shifter {shifter_state} ({shifter_slot})"
            shifter_stage = details.get("shifter_stage") or ""
            if shifter_stage:
                state_line += f", stage {shifter_stage}"
            shift_attempts = int(details.get("shifter_shift_attempts") or 0)
            if shift_attempts:
                state_line += f", shift tries {shift_attempts}"
        lines.append(state_line)
        equipped_error = str(details.get("equipped_probe_error") or "").strip()
        if equipped_error:
            lines.append(f"Equipped probe error: {equipped_error}")
        shifter_error = str(details.get("shifter_last_error") or "").strip()
        if shifter_error:
            lines.append(f"Shifter error: {shifter_error}")
        last_swap_feedback = str(details.get("last_swap_feedback") or "").replace("_", " ")
        if last_swap_feedback:
            lines.append(f"Last swap feedback: {last_swap_feedback}")
        for weapon in weapons:
            marker = "* " if weapon.get("current") else ""
            if weapon.get("pending"):
                marker = "> "
            lines.append(
                f"{marker}{weapon.get('key', '?')}/{weapon.get('label', '?')}: "
                f"{weapon.get('summary', 'Unknown')}"
            )
        combat = dict(details.get("combat", {}))
        if combat:
            lines.append(
                "Combat seen: "
                f"attacks {combat.get('attack_matched', 0)}/{combat.get('attack_seen', 0)}, "
                f"damage {combat.get('damage_matched', 0)}/{combat.get('damage_seen', 0)}, "
                f"parse misses {combat.get('damage_parse_miss', 0)}"
            )
            ignored = combat.get("ignored_attack_actor") or combat.get("ignored_damage_actor")
            if ignored:
                lines.append(f"Last ignored actor: {ignored}")
        self.weapon_learning_var.set("Learned weapons:\n" + "\n".join(lines))

    def on_expand_toggle(self):
        self.expanded = not self.expanded
        self._apply_expanded_state()

    def _apply_expanded_state(self):
        if self.expanded:
            self.body.grid(row=1, column=1, sticky="ew")
            self.expand_button.configure(text="Hide Settings")
        else:
            self.body.grid_remove()
            self.expand_button.configure(text="Show Settings")
        self.app.refresh_scroll_regions()

    def on_advanced_toggle(self):
        self.advanced_expanded = not self.advanced_expanded
        if self.advanced_expanded:
            self.advanced_body.grid()
            self.advanced_toggle_var.set("Hide Advanced")
        else:
            self.advanced_body.grid_remove()
            self.advanced_toggle_var.set("Show Advanced")
        self.app.refresh_scroll_regions()

    def on_auto_damage_mode_changed(self, _event=None):
        mode = str(self.vars["mode"][1].get()).strip()
        is_weapon = mode in AUTO_DAMAGE_WEAPON_MODES
        is_shifter = mode == AutoAAScript.MODE_SHIFTER_WEAPON_SWAP
        is_gi = mode == AutoAAScript.MODE_GNOMISH_INVENTOR

        if is_weapon:
            max_bindings = _weapon_mode_limit(mode)
            if is_shifter:
                self.mode_hint_var.set(
                    f"{mode} learns weapon damage from combat and unshifts for initial learning, healing avoidance, or a safe weapon that beats the Shift Gain threshold. "
                    f"Select up to {max_bindings} base weapon buttons and the quickbar button that shifts back into form. The starting weapon is assumed Unknown and reconciled from combat."
                )
                self.weapon_limit_var.set(
                    "Shifter flow: lock the current target, !cancel poly, wait for Player Hide, swap the weapon, then retry the shift slot once per second until the form is confirmed. Enable Heal Only to ignore damage gain and keep the old healing-only behavior."
                )
            else:
                self.mode_hint_var.set(
                    f"{mode} swaps weapons by quickbar and learns each weapon's damage profile from combat log lines, including adaptive P2-style signatures and rolling damage estimates. "
                    f"Select up to {max_bindings} weapon buttons. The starting weapon is assumed Unknown and reconciled from combat."
                )
                self.weapon_limit_var.set(
                    "Round delay: the swap lands at the start of the next combat round. "
                    "The script keeps the current weapon unless another clears the configured Gain % margin, "
                    "and treats one-off type changes as swap/boundary noise first."
                )
            self.command_section.grid_remove()
            self.weapon_section.grid()
        else:
            self.weapon_section.grid_remove()
            self.command_section.grid()
            if is_gi:
                self.mode_hint_var.set(
                    "Gnomish Inventor switches bolt type by chat command. The canister loop can be enabled or disabled here."
                )
            else:
                self.mode_hint_var.set(
                    "This mode switches damage by unfocused chat command. Weapon quickbar selection stays hidden."
                )

        self._set_holder_visible("auto_canister", is_gi)
        self._set_holder_visible("canister_cooldown_seconds", is_gi)
        self._set_holder_visible("shift_slot", is_shifter)
        self._set_holder_visible("shifter_min_swap_gain_percent", is_shifter)
        self._set_holder_visible("shifter_healing_only", is_shifter)
        self._set_holder_visible("min_swap_gain_percent", is_weapon and not is_shifter)
        self._update_weapon_selector_ui()
        self.app.refresh_scroll_regions()

    def _set_holder_visible(self, field_key, visible):
        holder = self.widget_holders.get(field_key)
        if holder is None:
            return
        if visible:
            holder.grid()
        else:
            holder.grid_remove()

    def on_weapon_slots_changed(self):
        self._update_weapon_selector_ui()

    def _selected_weapon_choices(self):
        selected = []
        for choice in WEAPON_SLOT_RENDER_ORDER:
            var, _widget = self.weapon_slot_vars[choice]
            if var.get():
                selected.append(choice)
        return selected

    def _update_weapon_selector_ui(self):
        if self.definition.script_id != "auto_aa":
            return

        selected = self._selected_weapon_choices()
        if selected:
            rendered = ", ".join(_weapon_choice_display(choice) for choice in selected)
            self.weapon_summary_var.set(f"Selected: {rendered}")
        else:
            self.weapon_summary_var.set("Selected: none")

        mode = str(self.vars["mode"][1].get()).strip()
        if mode in AUTO_DAMAGE_WEAPON_MODES:
            max_bindings = _weapon_mode_limit(mode)
            if len(selected) > max_bindings:
                self.weapon_limit_var.set(
                    f"Selected {len(selected)} weapon buttons, but {mode} only supports {max_bindings}. Trim the selection before starting."
                )
            elif mode == AutoAAScript.MODE_SHIFTER_WEAPON_SWAP:
                self.weapon_limit_var.set(
                    "Shifter mode starts Unknown, learns from outgoing damage, and unshifts only for healing avoidance, initial learning, or a safe weapon above Shift Gain %. It then uses the Shift slot until a shift message or essence line confirms the form."
                )
            else:
                self.weapon_limit_var.set(
                    "Round delay: the swap lands at the start of the next combat round. "
                    "From Unknown, the script probes from combat, treats physical-only hits as Unarmed, and builds approximate per-type damage estimates over time."
                )

    def on_auto_start_changed(self):
        if self.loaded_client_pid is None:
            return
        self.app.set_script_autostart(
            self.loaded_client_pid,
            self.definition.script_id,
            bool(self.auto_start_var.get()),
        )

    def on_coordinate_saved_lead_changed(self):
        if self.loaded_client_pid is None:
            return
        enabled = bool(self.coordinate_saved_lead_var.get())
        config = self._current_coordinate_follow_config_for_role_change(enabled)
        self.app.set_coordinate_follow_saved_lead(
            self.loaded_client_pid,
            enabled,
            config=config,
            enable_saved=enabled,
        )
        if enabled:
            self.auto_start_var.set(True)

    def _current_coordinate_follow_config_for_role_change(self, enabled):
        try:
            config = self.parse_config(validate_for_start=False)
        except Exception:
            config = self.app.get_script_config(self.loaded_client_pid, COORDINATE_FOLLOW_SCRIPT_ID)
        config["role"] = CoordinateFollowScript.ROLE_LEAD if enabled else CoordinateFollowScript.ROLE_FOLLOWER
        return config

    def on_toggle(self):
        try:
            config = self.parse_config()
        except Exception as exc:
            messagebox.showerror("HGCC", str(exc))
            return
        self.app.toggle_script(self.definition.script_id, config)

    def on_start_coordinate_lead(self):
        self._start_coordinate_follow_role(CoordinateFollowScript.ROLE_LEAD)

    def on_start_coordinate_follower(self):
        self._start_coordinate_follow_role(CoordinateFollowScript.ROLE_FOLLOWER)

    def _start_coordinate_follow_role(self, role):
        try:
            config = self.parse_config()
        except Exception as exc:
            messagebox.showerror("HGCC", str(exc))
            return
        config["role"] = role
        if hasattr(self, "coordinate_saved_lead_var"):
            self.coordinate_saved_lead_var.set(role == CoordinateFollowScript.ROLE_LEAD)
        self.app.start_script(self.definition.script_id, config)

    def on_assign_lead(self):
        self.app.assign_auto_attack_lead_async()


class SimKeysDesktopApp:
    def __init__(self, root, args):
        self.root = root
        self.args = args
        self.root.title("HG Control Console")
        self.root.geometry("1500x930")
        self.root.minsize(1240, 780)

        self.event_queue = queue.Queue()
        try:
            self.damage_meter_log_dir = damage_meter.reset_session_logs()
        except Exception as exc:
            self.damage_meter_log_dir = damage_meter.session_log_dir()
            self.log(f"Damage meter session log reset failed: {exc}", "error")
        self.script_manager = ScriptManager(self.enqueue_event)
        self.clients = []
        self.clients_by_pid = {}
        self.selected_pid = None
        self.refresh_in_progress = False
        self.script_configs = {}
        self.script_autostart = {}
        self.script_toggles_in_progress = {}
        self.character_script_configs = {}
        self.character_script_autostart = {}
        self.character_script_autostart_disabled = {}
        self.character_display_names = {}
        self.auto_loaded_character_keys = {}
        self.default_started_scripts = set()
        self.character_defaults_path = os.path.join(runtime.root_dir(), "data", "character_defaults.user.json")

        self.status_var = tk.StringVar(value="Ready")
        self.selected_name_var = tk.StringVar(value="No client selected")
        self.selected_details_var = tk.StringVar(value="Select an NWN client to see details.")
        self.chat_entry_var = tk.StringVar()
        self.auto_refresh_var = tk.BooleanVar(value=True)
        self.manual_controls_expanded = False
        self.manual_controls_toggle_var = tk.StringVar(value="Show Test Controls")
        self.target_analysis_expanded = False
        self.target_analysis_toggle_var = tk.StringVar(value="Show Target Analysis")
        self.damage_meter_expanded = False
        self.damage_meter_toggle_var = tk.StringVar(value="Show Damage Meter")
        self.damage_meter_status_var = tk.StringVar(value="No damage calculated.")
        self.damage_meter_progress_var = tk.DoubleVar(value=0.0)
        self.damage_meter_running = False
        self.damage_meter_run_id = 0
        self.damage_meter_summary = None
        self.activity_log_expanded = False
        self.activity_log_toggle_var = tk.StringVar(value="Show Activity Log")
        self.target_analysis_text = None
        self.damage_meter_text = None
        self.damage_meter_progress_frame = None
        self.damage_meter_progress = None
        self.damage_meter_calculate_button = None
        self.damage_meter_archive_button = None
        self.log_text = None
        self.sections_scroller = None
        self.script_container = None
        self.scroll_refresh_after_id = None
        self.closing = False
        self.target_analysis_frame = None
        self.damage_meter_frame = None
        self.activity_log_frame = None

        self._configure_style()
        self._build_ui()
        self._load_character_defaults_store()
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.after(100, self.process_events)
        self.root.after(150, self.refresh_clients_async)
        self.root.after(self.args.refresh_ms, self.auto_refresh_tick)

    def _normalize_character_key(self, name):
        return str(name or "").strip().casefold()

    def _default_script_autostart(self, script_id):
        return str(script_id or "") in DEFAULT_AUTO_START_SCRIPT_IDS

    def _clean_script_config(self, script_id, config):
        if script_id not in self.script_manager.registry or not isinstance(config, dict):
            return {}
        allowed = set(self.script_manager.default_config(script_id).keys())
        cleaned = {
            key: value
            for key, value in dict(config).items()
            if key in allowed
        }
        if script_id == COORDINATE_FOLLOW_SCRIPT_ID:
            try:
                distance = float(cleaned.get("distance_threshold", CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD))
            except (TypeError, ValueError):
                distance = CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD
            if abs(distance - 0.75) < 0.0001 or abs(distance - 0.10) < 0.0001:
                cleaned["distance_threshold"] = CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD
            if "distance_threshold" not in cleaned:
                cleaned["distance_threshold"] = CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD
            if "formation_radius" not in cleaned:
                cleaned["formation_radius"] = CoordinateFollowScript.DEFAULT_FORMATION_RADIUS
            if "bypass_no_walk" not in cleaned:
                cleaned["bypass_no_walk"] = True
        return cleaned

    def _load_character_defaults_store(self):
        self.character_script_configs = {}
        self.character_script_autostart = {}
        self.character_script_autostart_disabled = {}
        self.character_display_names = {}
        path = self.character_defaults_path
        if not os.path.isfile(path):
            return
        try:
            with open(path, "r", encoding="utf-8-sig") as handle:
                payload = json.load(handle)
        except Exception as exc:
            self.log(f"Character defaults load failed: {exc}", "error")
            return

        try:
            payload_version = int(payload.get("version") or 0) if isinstance(payload, dict) else 0
        except (TypeError, ValueError):
            payload_version = 0
        characters = payload.get("characters", {}) if isinstance(payload, dict) else {}
        if not isinstance(characters, dict):
            return

        for key, entry in characters.items():
            normalized = self._normalize_character_key(key)
            if not normalized or not isinstance(entry, dict):
                continue
            name = str(entry.get("name") or key).strip()
            scripts = entry.get("scripts", {})
            if not isinstance(scripts, dict):
                scripts = {}
            cleaned = {}
            for script_id, config in scripts.items():
                if script_id not in self.script_manager.registry or not isinstance(config, dict):
                    continue
                cleaned_config = self._clean_script_config(script_id, config)
                if payload_version < 3 and script_id == TIMERS_SCRIPT_ID:
                    try:
                        offset_y = int(cleaned_config.get("offset_y") or 0)
                    except (TypeError, ValueError):
                        offset_y = 0
                    if str(cleaned_config.get("position") or "TR").upper() == "TR" and offset_y == 0:
                        cleaned_config["offset_y"] = DEFAULT_TIMER_OVERLAY_OFFSET_Y
                if payload_version < 5 and script_id == COORDINATE_FOLLOW_SCRIPT_ID:
                    try:
                        distance = float(cleaned_config.get("distance_threshold", CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD))
                    except (TypeError, ValueError):
                        distance = CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD
                    if abs(distance - 0.0) < 0.0001 or abs(distance - 0.75) < 0.0001 or abs(distance - 0.10) < 0.0001:
                        cleaned_config["distance_threshold"] = CoordinateFollowScript.DEFAULT_DISTANCE_THRESHOLD
                    try:
                        radius = float(cleaned_config.get("formation_radius", CoordinateFollowScript.DEFAULT_FORMATION_RADIUS))
                    except (TypeError, ValueError):
                        radius = CoordinateFollowScript.DEFAULT_FORMATION_RADIUS
                    if abs(radius - 0.0) < 0.0001 or abs(radius - 4.0) < 0.0001:
                        cleaned_config["formation_radius"] = CoordinateFollowScript.DEFAULT_FORMATION_RADIUS
                    if not bool(cleaned_config.get("bypass_no_walk", False)):
                        cleaned_config["bypass_no_walk"] = True
                cleaned[script_id] = cleaned_config

            auto_start = entry.get("auto_start", [])
            disabled_auto_start = set()
            if isinstance(auto_start, dict):
                auto_start_items = [script_id for script_id, enabled in auto_start.items() if enabled]
                disabled_auto_start = {
                    str(script_id)
                    for script_id, enabled in auto_start.items()
                    if not enabled
                    and str(script_id) in self.script_manager.registry
                    and self._default_script_autostart(str(script_id))
                }
            elif isinstance(auto_start, list):
                auto_start_items = auto_start
            else:
                auto_start_items = []
            cleaned_auto_start = {
                str(script_id)
                for script_id in auto_start_items
                if str(script_id) in self.script_manager.registry
            }

            if not cleaned and not cleaned_auto_start and not disabled_auto_start:
                continue
            self.character_script_configs[normalized] = cleaned
            self.character_script_autostart[normalized] = cleaned_auto_start
            self.character_script_autostart_disabled[normalized] = disabled_auto_start
            self.character_display_names[normalized] = name

    def _save_character_defaults_store(self):
        payload = {"version": 5, "characters": {}}
        character_keys = (
            set(self.character_script_configs.keys())
            | set(self.character_script_autostart.keys())
            | set(self.character_script_autostart_disabled.keys())
        )
        for key in sorted(character_keys):
            scripts = {
                script_id: self._clean_script_config(script_id, config)
                for script_id, config in (self.character_script_configs.get(key) or {}).items()
                if script_id in self.script_manager.registry
            }
            auto_start = sorted(self.character_script_autostart.get(key) or set())
            disabled_auto_start = sorted(self.character_script_autostart_disabled.get(key) or set())
            if not scripts and not auto_start and not disabled_auto_start:
                continue
            entry = {
                "name": self.character_display_names.get(key, key),
                "scripts": scripts,
            }
            auto_start_payload = {
                script_id: True
                for script_id in auto_start
                if script_id in self.script_manager.registry
            }
            for script_id in disabled_auto_start:
                if script_id in self.script_manager.registry and self._default_script_autostart(script_id):
                    auto_start_payload[script_id] = False
            if auto_start_payload:
                entry["auto_start"] = {
                    script_id: auto_start_payload[script_id]
                    for script_id in sorted(auto_start_payload)
                }
            payload["characters"][key] = entry

        os.makedirs(os.path.dirname(self.character_defaults_path), exist_ok=True)
        with open(self.character_defaults_path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True)

    def _save_character_defaults_for_client(self, client_pid):
        client = self.clients_by_pid.get(client_pid)
        if client is None or not getattr(client, "character_name", None):
            return False

        character_key = self._normalize_character_key(client.character_name)
        if not character_key:
            return False

        scripts = {}
        for script_id in self.script_manager.registry.keys():
            scripts[script_id] = self.get_script_config(client_pid, script_id)

        auto_start = set(self.get_script_autostart_ids(client_pid))
        disabled_auto_start = {
            script_id
            for script_id in DEFAULT_AUTO_START_SCRIPT_IDS
            if script_id in self.script_manager.registry
            and not self.get_script_autostart(client_pid, script_id)
        }
        if (
            self.character_script_configs.get(character_key) == scripts
            and set(self.character_script_autostart.get(character_key) or set()) == auto_start
            and set(self.character_script_autostart_disabled.get(character_key) or set()) == disabled_auto_start
        ):
            return False

        self.character_script_configs[character_key] = scripts
        self.character_script_autostart[character_key] = auto_start
        self.character_script_autostart_disabled[character_key] = disabled_auto_start
        self.character_display_names[character_key] = client.character_name
        self._save_character_defaults_store()
        return True

    def _auto_load_character_defaults(self, record):
        if record is None or not record.character_name:
            return False

        character_key = self._normalize_character_key(record.character_name)
        if not character_key:
            return False

        if self.auto_loaded_character_keys.get(record.pid) == character_key:
            return False

        scripts = self.character_script_configs.get(character_key) or {}
        auto_start = set(self.character_script_autostart.get(character_key) or set())
        disabled_auto_start = set(self.character_script_autostart_disabled.get(character_key) or set())
        self.auto_loaded_character_keys[record.pid] = character_key
        for script_id in self.script_manager.registry.keys():
            if script_id in disabled_auto_start:
                enabled = False
            elif script_id in auto_start:
                enabled = True
            else:
                enabled = self._default_script_autostart(script_id)
            self.script_autostart[(record.pid, script_id)] = enabled
        if not scripts and not auto_start and not disabled_auto_start:
            return False

        for script_id, config in scripts.items():
            self.script_configs[(record.pid, script_id)] = self._clean_script_config(script_id, config)

        self.log(f"{record.display_name}: loaded saved character defaults", "info")
        return True

    def _configure_style(self):
        style = ttk.Style()
        for theme in ("vista", "xpnative", "clam"):
            try:
                style.theme_use(theme)
                break
            except tk.TclError:
                continue

    def _build_ui(self):
        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=1)
        outer.rowconfigure(1, weight=1)

        toolbar = ttk.Frame(outer)
        toolbar.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        toolbar.columnconfigure(8, weight=1)

        ttk.Button(toolbar, text="Refresh Clients", command=self.refresh_clients_async).grid(row=0, column=0, padx=(0, 8))
        ttk.Button(toolbar, text="Inject Next", command=self.inject_next_async).grid(row=0, column=1, padx=(0, 8))
        ttk.Button(toolbar, text="Inject All", command=self.inject_all_async).grid(row=0, column=2, padx=(0, 8))
        ttk.Button(toolbar, text="Start Saved", command=self.start_saved_scripts_all_async).grid(row=0, column=3, padx=(0, 8))
        ttk.Button(toolbar, text="Stop All Scripts", command=self.stop_all_scripts_async).grid(row=0, column=4, padx=(0, 8))
        ttk.Checkbutton(toolbar, text="Auto Refresh", variable=self.auto_refresh_var).grid(row=0, column=5, padx=(0, 8))
        ttk.Label(toolbar, text="Selection:").grid(row=0, column=6, padx=(8, 4))
        ttk.Label(toolbar, textvariable=self.selected_name_var).grid(row=0, column=7, sticky="w")
        ttk.Label(toolbar, text=f"Inject Python: {self.args.inject_python or os.path.basename(sys.executable)}").grid(row=0, column=9, sticky="e")

        paned = ttk.Panedwindow(outer, orient="horizontal")
        paned.grid(row=1, column=0, sticky="nsew")
        self.main_paned = paned
        paned.bind("<Configure>", self._on_main_paned_event)
        paned.bind("<ButtonRelease-1>", self._on_main_paned_event)

        left = ttk.Frame(paned, padding=(0, 0, 10, 0))
        left.columnconfigure(0, weight=1)
        left.rowconfigure(1, weight=1)
        paned.add(left, weight=1)

        client_header = ttk.Frame(left)
        client_header.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 6))
        client_header.columnconfigure(0, weight=1)
        ttk.Label(client_header, text="Discovered Clients").grid(row=0, column=0, sticky="w")
        ttk.Label(client_header, text="|| Drag divider", foreground="#6c757d").grid(row=0, column=1, sticky="e")
        self.client_tree = ttk.Treeview(
            left,
            columns=("ord", "pid", "injected", "name", "window", "started", "scripts"),
            show="headings",
            selectmode="browse",
            height=10,
        )
        for col, title, width, anchor in (
            ("ord", "#", 45, "center"),
            ("pid", "PID", 75, "center"),
            ("injected", "Injected", 70, "center"),
            ("name", "Character", 170, "w"),
            ("window", "Window", 260, "w"),
            ("started", "Started", 150, "w"),
            ("scripts", "Scripts", 70, "center"),
        ):
            self.client_tree.heading(col, text=title)
            self.client_tree.column(col, width=width, anchor=anchor, stretch=False)
        self.client_tree.grid(row=1, column=0, sticky="nsew")
        client_scroll = ttk.Scrollbar(left, orient="vertical", command=self.client_tree.yview)
        client_scroll.grid(row=1, column=1, sticky="ns")
        client_xscroll = ttk.Scrollbar(left, orient="horizontal", command=self.client_tree.xview)
        client_xscroll.grid(row=2, column=0, sticky="ew")
        self.client_tree.configure(yscrollcommand=client_scroll.set, xscrollcommand=client_xscroll.set)
        self.client_tree.bind("<<TreeviewSelect>>", self.on_client_selected)

        self.sections_scroller = ScrollableFrame(paned, stretch_height=True)
        self.sections_scroller.interior.columnconfigure(0, weight=1)
        right = self.sections_scroller.interior
        paned.add(self.sections_scroller, weight=5)
        self.root.after(250, self._set_initial_pane_sizes)

        details = ttk.LabelFrame(right, text="Client Details", padding=10)
        details.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        details.columnconfigure(0, weight=1)
        self.details_label = ttk.Label(details, textvariable=self.selected_details_var, justify="left", wraplength=520)
        self.details_label.grid(row=0, column=0, sticky="ew")
        details.bind("<Configure>", self._on_details_resize)

        actions = ttk.LabelFrame(right, text="Manual Test Controls", padding=10)
        actions.grid(row=1, column=0, sticky="ew", pady=(0, 10))
        actions.columnconfigure(0, weight=1)

        actions_header = ttk.Frame(actions)
        actions_header.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        actions_header.columnconfigure(0, weight=1)
        self.manual_intro_label = ttk.Label(
            actions_header,
            text="Quickbar button presses and raw chat sends live here for manual testing and reverse engineering.",
            justify="left",
            wraplength=420,
        )
        self.manual_intro_label.grid(row=0, column=0, sticky="ew")
        actions.bind("<Configure>", self._on_manual_controls_resize)
        ttk.Button(
            actions_header,
            textvariable=self.manual_controls_toggle_var,
            command=self.toggle_manual_controls,
            width=18,
        ).grid(row=0, column=1, padx=(12, 0), sticky="e")

        self.manual_controls_body = ttk.Frame(actions)
        self.manual_controls_body.grid(row=1, column=0, sticky="ew")
        self.manual_controls_body.columnconfigure(0, weight=1)

        quickbar = ttk.LabelFrame(self.manual_controls_body, text="Quickbar Banks", padding=8)
        quickbar.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        quickbar.columnconfigure(1, weight=1)
        ttk.Label(
            quickbar,
            text="Page 0 is the normal bar, page 1 matches Shift+F1..F12, and page 2 matches Ctrl+F1..F12.",
        ).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 6))

        for bank_row, (page, label) in enumerate(((0, "Base"), (1, "Shift"), (2, "Ctrl")), start=1):
            ttk.Label(quickbar, text=label, width=7).grid(row=bank_row, column=0, sticky="w", padx=(0, 8))
            row_frame = ttk.Frame(quickbar)
            row_frame.grid(row=bank_row, column=1, sticky="ew", pady=2)
            for slot in range(1, 13):
                ttk.Button(
                    row_frame,
                    text=f"F{slot}",
                    width=5,
                    command=lambda value=slot, bank_page=page, bank_label=label: self.trigger_slot_async(value, bank_page, bank_label),
                ).grid(
                    row=0,
                    column=slot - 1,
                    padx=2,
                    pady=2,
                    sticky="ew",
                )

        chat_row = ttk.Frame(self.manual_controls_body)
        chat_row.grid(row=1, column=0, sticky="ew")
        chat_row.columnconfigure(1, weight=1)
        ttk.Label(chat_row, text="Chat").grid(row=0, column=0, padx=(0, 8))
        ttk.Entry(chat_row, textvariable=self.chat_entry_var).grid(row=0, column=1, sticky="ew")
        ttk.Button(chat_row, text="Send", command=self.send_chat_async).grid(row=0, column=2, padx=(8, 0))

        self._apply_manual_controls_state()

        damage_frame = ttk.LabelFrame(right, text="Damage Meter", padding=10)
        self.damage_meter_frame = damage_frame
        damage_frame.grid(row=2, column=0, sticky="ew", pady=(0, 10))
        damage_frame.columnconfigure(0, weight=1)
        damage_header = ttk.Frame(damage_frame)
        damage_header.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        damage_header.columnconfigure(0, weight=1)
        ttk.Label(damage_header, textvariable=self.damage_meter_status_var).grid(row=0, column=0, sticky="w")
        self.damage_meter_calculate_button = ttk.Button(
            damage_header,
            text="Calculate",
            command=self.calculate_damage_meter_async,
            width=12,
        )
        self.damage_meter_calculate_button.grid(row=0, column=1, padx=(8, 0))
        self.damage_meter_archive_button = ttk.Button(
            damage_header,
            text="Analyze Archive",
            command=self.calculate_damage_meter_archive_async,
            width=15,
        )
        self.damage_meter_archive_button.grid(row=0, column=2, padx=(6, 0))
        ttk.Button(damage_header, text="Post Net", command=lambda: self.post_damage_meter_async("net"), width=10).grid(row=0, column=3, padx=(6, 0))
        ttk.Button(damage_header, text="Post Raw", command=lambda: self.post_damage_meter_async("raw"), width=10).grid(row=0, column=4, padx=(6, 0))
        ttk.Button(damage_header, text="Post Healing", command=lambda: self.post_damage_meter_async("healing"), width=12).grid(row=0, column=5, padx=(6, 0))
        ttk.Button(damage_header, text="Post Elements", command=lambda: self.post_damage_meter_async("breakdown"), width=13).grid(row=0, column=6, padx=(6, 0))
        ttk.Button(
            damage_header,
            textvariable=self.damage_meter_toggle_var,
            command=self.toggle_damage_meter,
            width=18,
        ).grid(row=0, column=7, padx=(12, 0), sticky="e")
        self.damage_meter_progress_frame = ttk.Frame(damage_frame)
        self.damage_meter_progress_frame.grid(row=1, column=0, sticky="ew", pady=(0, 6))
        self.damage_meter_progress_frame.columnconfigure(0, weight=1)
        self.damage_meter_progress = ttk.Progressbar(
            self.damage_meter_progress_frame,
            variable=self.damage_meter_progress_var,
            maximum=100.0,
            mode="determinate",
        )
        self.damage_meter_progress.grid(row=0, column=0, sticky="ew")
        self.damage_meter_progress_frame.grid_remove()
        self.damage_meter_text = ScrolledText(damage_frame, wrap="word", height=16, font=("Consolas", 9))
        self.damage_meter_text.grid(row=2, column=0, sticky="ew")
        self.damage_meter_text.configure(state="disabled")
        self._set_damage_meter_text("Press Calculate to summarize this HGCC GUI session.")
        self._apply_damage_meter_state()

        analysis_stack = ttk.Frame(right)
        analysis_stack.grid(row=3, column=0, sticky="ew")
        analysis_stack.columnconfigure(0, weight=1)

        target = ttk.LabelFrame(analysis_stack, text="Target Analysis", padding=10)
        self.target_analysis_frame = target
        target.grid(row=0, column=0, sticky="ew", pady=(0, 10))
        target.columnconfigure(0, weight=1)
        target.rowconfigure(1, weight=1)
        target_header = ttk.Frame(target)
        target_header.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        target_header.columnconfigure(0, weight=1)
        ttk.Label(
            target_header,
            text="Current target resistances, healing, and learned weapon estimates.",
        ).grid(row=0, column=0, sticky="w")
        ttk.Button(
            target_header,
            textvariable=self.target_analysis_toggle_var,
            command=self.toggle_target_analysis,
            width=20,
        ).grid(row=0, column=1, padx=(12, 0), sticky="e")
        self.target_analysis_text = ScrolledText(target, wrap="word", height=14, font=("Consolas", 9))
        self.target_analysis_text.grid(row=1, column=0, sticky="nsew")
        self.target_analysis_text.configure(state="disabled")
        self._set_target_analysis_text("Start Auto Damage in Weapon Swap mode to see target resistances and weapon estimates.")

        scripts = ttk.LabelFrame(analysis_stack, text="Automation", padding=10)
        scripts.grid(row=1, column=0, sticky="ew", pady=(0, 10))
        scripts.columnconfigure(0, weight=1)
        self.script_container = ttk.Frame(scripts)
        self.script_container.grid(row=0, column=0, sticky="ew")
        self.script_container.columnconfigure(0, weight=1)
        self.script_rows = {}
        for row_index, definition in enumerate(self.script_manager.definitions()):
            row = ScriptCard(self.script_container, definition, self)
            row.grid(row=row_index, column=0, sticky="ew", pady=(0, 4))
            self.script_rows[definition.script_id] = row

        logs = ttk.LabelFrame(analysis_stack, text="Activity Log", padding=10)
        self.activity_log_frame = logs
        logs.grid(row=2, column=0, sticky="ew")
        logs.columnconfigure(0, weight=1)
        logs.rowconfigure(1, weight=1)
        logs_header = ttk.Frame(logs)
        logs_header.grid(row=0, column=0, sticky="ew", pady=(0, 6))
        logs_header.columnconfigure(0, weight=1)
        ttk.Label(
            logs_header,
            text="Recent automation, connection, and script events.",
        ).grid(row=0, column=0, sticky="w")
        ttk.Button(
            logs_header,
            textvariable=self.activity_log_toggle_var,
            command=self.toggle_activity_log,
            width=18,
        ).grid(row=0, column=1, padx=(12, 0), sticky="e")
        self.log_text = ScrolledText(logs, wrap="word", height=18, font=("Consolas", 10))
        self.log_text.grid(row=1, column=0, sticky="nsew")
        self.log_text.configure(state="disabled")
        self._apply_target_analysis_state()
        self._apply_activity_log_state()

        status_bar = ttk.Label(outer, textvariable=self.status_var, anchor="w")
        status_bar.grid(row=2, column=0, sticky="ew", pady=(10, 0))

    def toggle_manual_controls(self):
        self.manual_controls_expanded = not self.manual_controls_expanded
        self._apply_manual_controls_state()

    def _apply_manual_controls_state(self):
        if self.manual_controls_expanded:
            self.manual_controls_body.grid()
            self.manual_controls_toggle_var.set("Hide Test Controls")
        else:
            self.manual_controls_body.grid_remove()
            self.manual_controls_toggle_var.set("Show Test Controls")
        self.refresh_scroll_regions()

    def toggle_damage_meter(self):
        self.damage_meter_expanded = not self.damage_meter_expanded
        self._apply_damage_meter_state()

    def _apply_damage_meter_state(self):
        if self.damage_meter_text is None:
            return
        if self.damage_meter_expanded:
            self.damage_meter_text.grid()
            self.damage_meter_toggle_var.set("Hide Damage Meter")
        else:
            self.damage_meter_text.grid_remove()
            self.damage_meter_toggle_var.set("Show Damage Meter")
        self.refresh_scroll_regions()

    def _set_damage_meter_text(self, text):
        if self.damage_meter_text is None:
            return
        self.damage_meter_text.configure(state="normal")
        self.damage_meter_text.delete("1.0", "end")
        self.damage_meter_text.insert("1.0", str(text or ""))
        self.damage_meter_text.configure(state="disabled")

    def calculate_damage_meter_async(self, source_path=None, source_label="current session", archived=False):
        if self.damage_meter_running:
            return
        self.damage_meter_run_id += 1
        run_id = self.damage_meter_run_id
        self.damage_meter_summary = None
        self._set_damage_meter_calculating(True)
        source_path = source_path or self.damage_meter_log_dir
        source_label = str(source_label or "current session")
        self.damage_meter_status_var.set(f"Calculating {source_label}...")
        self._set_damage_meter_text(f"Calculating damage meter from {source_label}. Long sessions may take a little while.")

        def worker():
            def progress(payload):
                event = dict(payload or {})
                event["type"] = "damage-meter-progress"
                event["run_id"] = run_id
                self.enqueue_event(event)

            try:
                if archived:
                    summary = damage_meter.analyze_archived_session(source_path, progress_callback=progress)
                else:
                    summary = damage_meter.analyze_session_logs(source_path, progress_callback=progress)
                text = damage_meter.format_summary_text(summary)
                text = f"Source: {source_label}\n{text}"
                report_path = ""
                try:
                    report_path = damage_meter.save_summary_text(summary, text)
                    text = f"{text}\n\nReport saved: {report_path}"
                except Exception as save_exc:
                    text = f"{text}\n\nReport save failed: {save_exc}"
                self.enqueue_event({
                    "type": "damage-meter-result",
                    "run_id": run_id,
                    "summary": summary,
                    "text": text,
                    "source_label": source_label,
                    "report_path": report_path,
                })
            except Exception as exc:
                self.enqueue_event({
                    "type": "damage-meter-error",
                    "run_id": run_id,
                    "message": f"Damage meter failed: {exc}",
                })

        threading.Thread(target=worker, name="SimKeysDamageMeter", daemon=True).start()

    def calculate_damage_meter_archive_async(self):
        archive_dir = damage_meter.session_archive_dir()
        initial_dir = archive_dir if os.path.isdir(archive_dir) else os.path.dirname(self.damage_meter_log_dir)
        path = filedialog.askopenfilename(
            parent=self.root,
            title="Analyze Damage Meter Archive",
            initialdir=initial_dir,
            filetypes=(
                ("Damage meter archives", "*.zip"),
                ("All files", "*.*"),
            ),
        )
        if not path:
            return
        self.calculate_damage_meter_async(
            source_path=path,
            source_label=os.path.basename(path),
            archived=True,
        )

    def _set_damage_meter_calculating(self, calculating):
        self.damage_meter_running = bool(calculating)
        if self.damage_meter_calculate_button is not None:
            self.damage_meter_calculate_button.configure(state="disabled" if calculating else "normal")
        if self.damage_meter_archive_button is not None:
            self.damage_meter_archive_button.configure(state="disabled" if calculating else "normal")
        if self.damage_meter_progress_frame is not None:
            if calculating:
                self.damage_meter_progress_frame.grid()
            else:
                self.damage_meter_progress_frame.grid_remove()
        if calculating:
            self.damage_meter_progress_var.set(0.0)
        self.refresh_scroll_regions()

    def _handle_damage_meter_progress(self, event):
        if int(event.get("run_id") or 0) != self.damage_meter_run_id:
            return
        percent = event.get("percent")
        if percent is not None:
            try:
                self.damage_meter_progress_var.set(min(max(float(percent), 0.0), 100.0))
            except (TypeError, ValueError):
                pass

        phase = str(event.get("phase") or "Calculating damage")
        current = int(event.get("current") or 0)
        total = int(event.get("total") or 0)
        if total > 0:
            self.damage_meter_status_var.set(f"{phase}: {current:,}/{total:,}")
        elif current > 0:
            self.damage_meter_status_var.set(f"{phase}: {current:,}")
        else:
            self.damage_meter_status_var.set(phase)

    def post_damage_meter_async(self, report_type):
        client = self.selected_client()
        if client is None or not client.injected:
            messagebox.showwarning("HGCC", "Select an injected client first.")
            return
        if self.damage_meter_summary is None:
            messagebox.showwarning("HGCC", "Calculate the damage meter first.")
            return

        lines = damage_meter.chat_report_lines(self.damage_meter_summary, report_type)
        if not lines:
            return

        def action():
            for line in lines:
                runtime.send_chat(client, line, 2)
            return f"{client.display_name}: posted damage meter {report_type}"

        self.run_background(f"Post Damage Meter {report_type}", action)

    def toggle_target_analysis(self):
        self.target_analysis_expanded = not self.target_analysis_expanded
        self._apply_target_analysis_state()

    def _apply_target_analysis_state(self):
        if self.target_analysis_text is None:
            return
        if self.target_analysis_expanded:
            self.target_analysis_text.grid()
            self.target_analysis_toggle_var.set("Hide Target Analysis")
        else:
            self.target_analysis_text.grid_remove()
            self.target_analysis_toggle_var.set("Show Target Analysis")
        self.refresh_scroll_regions()

    def toggle_activity_log(self):
        self.activity_log_expanded = not self.activity_log_expanded
        self._apply_activity_log_state()

    def _apply_activity_log_state(self):
        if self.log_text is None:
            return
        if self.activity_log_expanded:
            self.log_text.grid()
            self.activity_log_toggle_var.set("Hide Activity Log")
        else:
            self.log_text.grid_remove()
            self.activity_log_toggle_var.set("Show Activity Log")
        self.refresh_scroll_regions()

    def refresh_scroll_regions(self):
        if self.closing or self.scroll_refresh_after_id is not None:
            return
        try:
            if not self.root.winfo_exists():
                return
            self.scroll_refresh_after_id = self.root.after_idle(self._run_refresh_scroll_regions)
        except tk.TclError:
            self.scroll_refresh_after_id = None
            pass

    def _run_refresh_scroll_regions(self):
        self.scroll_refresh_after_id = None
        if self.closing:
            return
        try:
            self._refresh_scroll_regions()
        except Exception as exc:
            self.log(f"Error while refreshing scrolled regions: {exc}", "error")

    def _refresh_scroll_regions(self):
        for scroller in (self.sections_scroller,):
            if scroller is None:
                continue
            try:
                if not scroller.winfo_exists():
                    continue
                scroller.refresh()
            except Exception:
                pass

    def _set_initial_pane_sizes(self):
        try:
            if self.main_paned.winfo_width() > 0:
                self.main_paned.sashpos(0, CLIENT_PANE_DEFAULT_WIDTH)
                self._enforce_main_paned_min_width()
        except tk.TclError:
            pass
        self.refresh_scroll_regions()

    def _on_main_paned_event(self, _event=None):
        self.root.after_idle(self._enforce_main_paned_min_width)

    def _enforce_main_paned_min_width(self):
        try:
            if self.main_paned is None or not self.main_paned.winfo_exists():
                return
            width = int(self.main_paned.winfo_width())
            if width <= 1:
                return
            minimum = min(CLIENT_PANE_MIN_WIDTH, max(width - 240, 120))
            if self.main_paned.sashpos(0) < minimum:
                self.main_paned.sashpos(0, minimum)
        except tk.TclError:
            pass

    def _on_details_resize(self, event):
        self.details_label.configure(wraplength=max(int(event.width) - 24, 240))

    def _on_manual_controls_resize(self, event):
        self.manual_intro_label.configure(wraplength=max(int(event.width) - 210, 220))

    def enqueue_event(self, event):
        self.event_queue.put(event)

    def log(self, message, level="info"):
        self.enqueue_event({"type": "log", "level": level, "message": message})

    def process_events(self):
        while True:
            try:
                event = self.event_queue.get_nowait()
            except queue.Empty:
                break
            self.handle_event(event)
        self.root.after(100, self.process_events)

    def handle_event(self, event):
        event_type = event.get("type")
        if event_type == "clients-refreshed":
            self.apply_client_records(event["records"])
            return
        if event_type == "refresh-finished":
            self.refresh_in_progress = False
            return
        if event_type == "script-state":
            self.persist_loaded_configs(self.selected_pid)
            self.refresh_selected_client_ui()
            self.refresh_client_tree_rows()
            return
        if event_type == "script-toggle-finished":
            self.script_toggles_in_progress.pop((event.get("client_pid"), event.get("script_id")), None)
            self.refresh_selected_client_ui()
            self.refresh_client_tree_rows()
            return
        if event_type == "overlay-script-toggle":
            self.handle_overlay_script_toggle(event)
            return
        if event_type == "damage-meter-progress":
            self._handle_damage_meter_progress(event)
            return
        if event_type == "damage-meter-result":
            if int(event.get("run_id") or 0) != self.damage_meter_run_id:
                return
            self._set_damage_meter_calculating(False)
            self.damage_meter_summary = event.get("summary")
            self._set_damage_meter_text(event.get("text", ""))
            if self.damage_meter_summary is not None:
                source_label = str(event.get("source_label") or "current session")
                self.damage_meter_status_var.set(
                    f"Damage ({source_label}): net {self.damage_meter_summary.net:,}   raw {self.damage_meter_summary.raw_damage:,}   healing {self.damage_meter_summary.raw_healing:,}"
                )
            self.damage_meter_expanded = True
            self._apply_damage_meter_state()
            return
        if event_type == "damage-meter-error":
            if int(event.get("run_id") or 0) != self.damage_meter_run_id:
                return
            self._set_damage_meter_calculating(False)
            message = event.get("message", "Damage meter failed.")
            self.damage_meter_status_var.set(message)
            self.append_log(message, "error")
            return
        if event_type == "log":
            self.append_log(event.get("message", ""), event.get("level", "info"))
            return

    def handle_overlay_script_toggle(self, event):
        try:
            client_pid = int(event.get("client_pid"))
        except (TypeError, ValueError):
            return

        script_id = str(event.get("script_id") or "").strip()
        if script_id not in self.script_manager.registry:
            self.log(f"Overlay requested unknown script '{script_id}' for pid {client_pid}", "error")
            return

        client = self.clients_by_pid.get(client_pid)
        if client is None:
            host = self.script_manager.hosts.get(client_pid)
            client = getattr(host, "client", None)
        if client is None or not getattr(client, "injected", False):
            self.log(f"Overlay requested {script_id}, but pid {client_pid} is not available.", "error")
            return

        config = self.get_script_config(client_pid, script_id)
        self.toggle_script_for_client(client, script_id, config, source="Overlay")

    def append_log(self, message, level="info"):
        if not message:
            return
        self.status_var.set(message)
        if self.log_text is None:
            return
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"[{level.upper()}] {message}\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _set_target_analysis_text(self, text):
        if self.target_analysis_text is None:
            return
        self.target_analysis_text.configure(state="normal")
        self.target_analysis_text.delete("1.0", "end")
        self.target_analysis_text.insert("1.0", text)
        self.target_analysis_text.configure(state="disabled")

    def _format_target_stat_entries(self, entries, value_suffix=""):
        entries = list(entries or [])
        if not entries:
            return "-"
        parts = []
        for entry in entries:
            label = str(entry.get("label") or entry.get("type") or "?")
            if "value" in entry:
                parts.append(f"{label} {entry.get('value')}{value_suffix}")
            else:
                parts.append(label)
        return ", ".join(parts)

    def _target_analysis_weapon_sort_key(self, weapon):
        selection = weapon.get("selection_damage")
        if selection is None:
            selection = -1
        return (
            0 if weapon.get("recommended") else 1,
            0 if weapon.get("current") else 1,
            0 if weapon.get("pending") else 1,
            0 if not weapon.get("healing_types") else 1,
            -int(selection),
            str(weapon.get("key") or ""),
        )

    def _compact_damage_label(self, label):
        label = str(label or "").strip()
        suffix = ""
        while label and not label[-1].isalnum():
            suffix = label[-1] + suffix
            label = label[:-1]
        compact = {
            "Acid": "Acid",
            "Bludgeoning": "Blud",
            "Cold": "Cold",
            "Divine": "Div",
            "Electrical": "Elec",
            "Fire": "Fire",
            "Magical": "Mag",
            "Negative": "Neg",
            "Piercing": "Pier",
            "Positive": "Pos",
            "Slashing": "Slsh",
            "Sonic": "Soni",
        }
        return compact.get(label, label) + suffix

    def _format_target_stat_entries_compact(self, entries, value_suffix=""):
        entries = list(entries or [])
        if not entries:
            return "-"
        parts = []
        for entry in entries:
            label = self._compact_damage_label(entry.get("label") or entry.get("type") or "?")
            if "value" in entry:
                parts.append(f"{label}{entry.get('value')}{value_suffix}")
            else:
                parts.append(label)
        return " ".join(parts)

    def _compact_weapon_type_text(self, weapon):
        summary_text = str(weapon.get("summary") or "").strip()
        if (
            str(weapon.get("special_name") or "").strip() == "P2"
            or "adaptive" in summary_text.lower()
        ):
            return "Adaptive"
        for prefix in ("Current ", "Types ", "Seen ", "Predicted "):
            marker = f"{prefix}"
            if marker in summary_text:
                fragment = summary_text.split(marker, 1)[1].split(", ", 1)[0]
                compact_parts = [
                    self._compact_damage_label(part)
                    for part in str(fragment).split("/")
                    if str(part).strip()
                ]
                if compact_parts:
                    return "/".join(compact_parts)
        if "Unknown" in summary_text:
            return "Unknown"
        return "-"

    def _compact_weapon_state_text(self, weapon):
        summary = str(weapon.get("summary") or "").strip()
        if "Learning Complete" in summary:
            return "done"
        if "P2 check " in summary:
            return summary.split("P2 check ", 1)[1].split(",", 1)[0].strip()
        if "adaptive" in summary.lower() or str(weapon.get("special_name") or "").strip() == "P2":
            return "P2"
        if "Unknown" in summary:
            return "unknown"
        return "learn"

    def _compact_weapon_special_tag_text(self, weapon):
        special_name = str(weapon.get("special_name") or "").strip()
        if special_name == "Mammon's Wrath":
            return "MW"
        if special_name == "P2":
            return "P2"
        return ""

    def _compact_weapon_notes_text(self, weapon):
        flags = []
        healing_types = [
            self._compact_damage_label(value)
            for value in list(weapon.get("healing_types") or [])
        ]
        if healing_types:
            flags.append("heal " + "/".join(healing_types))

        ignored_types = [
            self._compact_damage_label(value)
            for value in list(weapon.get("ignored_types") or [])
        ]
        if ignored_types:
            flags.append("ign " + "/".join(ignored_types))
        return ", ".join(flags)

    def _compact_weapon_damage_text(self, weapon):
        expected = weapon.get("expected_damage")
        actual = weapon.get("actual_damage")
        actual_obs = int(weapon.get("actual_observations") or 0)
        expected_text = f"e{int(expected)}" if expected is not None else "e-"
        if actual is not None and actual_obs > 0:
            actual_text = f"a{int(actual)}"
        else:
            actual_text = "a-"
        return f"{expected_text:<6}{actual_text:<8}".rstrip()

    def _render_target_analysis(self, details):
        analysis = dict(details.get("target_analysis", {}))
        target = str(analysis.get("target") or "").strip()
        if not target:
            return "Waiting for Auto Damage to observe your next attack target."

        lines = []
        if not analysis.get("available"):
            message = str(analysis.get("message") or f"No data for '{target}'.")
            return f"Target: {target}\nStatus: {message}"

        matched = str(analysis.get("matched_name") or target)
        paragon = int(analysis.get("paragon_ranks") or 0)
        target_line = f"Target: {target}"
        if matched and matched != target:
            target_line += f" | Entry: {matched}"
        target_line += f" | Paragon {paragon}"
        lines.append(target_line)

        special_rule = str(analysis.get("special_target_rule") or "").strip()
        if special_rule:
            lines.append(f"Rule: {special_rule}")
        lines.append(f"Imm:  {self._format_target_stat_entries_compact(analysis.get('immunity'), '%')}")
        lines.append(f"Res:  {self._format_target_stat_entries_compact(analysis.get('resistance'))}")
        lines.append(f"Heal: {self._format_target_stat_entries_compact(analysis.get('healing'))}")

        weapons = list(analysis.get("weapons") or [])

        recommended = next((weapon for weapon in weapons if weapon.get("recommended")), None)
        lines.append("")
        if recommended is None:
            lines.append("Best: -")
        else:
            recommendation_name = f"{recommended.get('key', '?')}/{recommended.get('label', '?')}"
            recommendation_damage = self._compact_weapon_damage_text(recommended)
            lines.append(f"Best: {recommendation_name} | {recommendation_damage}")

        lines.append("")
        if not weapons:
            lines.append("No learned weapon profiles yet.")
            return "\n".join(lines)

        for weapon in weapons:
            markers = ""
            if weapon.get("current"):
                markers += "*"
            if weapon.get("pending"):
                markers += ">"
            if weapon.get("recommended"):
                markers += "!"
            marker_text = f"{markers:3}" if markers else "   "

            key = str(weapon.get("key") or "?")
            label = str(weapon.get("label") or "?")
            slot_text = f"{key}/{label}"
            special_tag = self._compact_weapon_special_tag_text(weapon)
            notes_text = self._compact_weapon_notes_text(weapon)
            damage_text = self._compact_weapon_damage_text(weapon)
            type_text = self._compact_weapon_type_text(weapon)
            state_text = self._compact_weapon_state_text(weapon)
            row = f"{marker_text} {slot_text:<12} {special_tag:<4}{damage_text:<14} {type_text:<18} {state_text}"
            if notes_text:
                row = f"{row} {notes_text}"
            lines.append(row)

        lines.append("")
        lines.append("e expected   a actual   MW Mammon's Wrath   P2 Adaptive")
        lines.append("* current   > pending   ! recommended")
        return "\n".join(lines)

    def refresh_target_analysis_panel(self):
        client = self.selected_client()
        if client is None:
            self._set_target_analysis_text("Select an NWN client to see target analysis.")
            return

        state = self.script_manager.get_state(client.pid, "auto_aa")
        if not state.get("running"):
            self._set_target_analysis_text("Start Auto Damage in Weapon Swap mode to see target resistances and weapon estimates.")
            return

        details = dict(state.get("details", {}))
        if not details.get("weapon_mode"):
            self._set_target_analysis_text("Target analysis is currently focused on Weapon Swap mode.")
            return

        self._set_target_analysis_text(self._render_target_analysis(details))

    def auto_refresh_tick(self):
        if self.auto_refresh_var.get():
            self.refresh_clients_async()
        self.root.after(self.args.refresh_ms, self.auto_refresh_tick)

    def run_background(self, label, fn, refresh_after=False):
        def worker():
            try:
                message = fn()
                if message:
                    self.log(message, "info")
            except Exception as exc:
                self.log(f"{label} failed: {exc}", "error")
            finally:
                if refresh_after:
                    self.refresh_clients_async()
        threading.Thread(target=worker, name=f"SimKeysTask-{label}", daemon=True).start()

    def refresh_clients_async(self):
        if self.refresh_in_progress:
            return
        self.refresh_in_progress = True

        def worker():
            try:
                records = runtime.discover_clients(process_name=self.args.process_name)
                self.enqueue_event({"type": "clients-refreshed", "records": records})
            except Exception as exc:
                self.log(f"Refresh failed: {exc}", "error")
            finally:
                self.enqueue_event({"type": "refresh-finished"})

        threading.Thread(target=worker, name="SimKeysRefresh", daemon=True).start()

    def persist_loaded_configs(self, client_pid):
        if client_pid is None:
            return
        changed = False
        for row in self.script_rows.values():
            if row.try_persist_for_client(client_pid):
                changed = True
        if changed:
            self._save_character_defaults_for_client(client_pid)

    def apply_client_records(self, records):
        self.persist_loaded_configs(self.selected_pid)
        previous_records = dict(self.clients_by_pid)
        old_selected = self.selected_pid
        for record in records:
            previous = previous_records.get(record.pid)
            if previous is None:
                continue
            preserve_injected = (
                not record.injected and (
                    self.script_manager.running_script_count(record.pid) > 0
                    or (previous.injected and _probe_error_is_busy(record.probe_error))
                )
            )
            if preserve_injected:
                record.injected = True
                if not record.character_name:
                    record.character_name = previous.character_name
                if record.player_object == 0:
                    record.player_object = previous.player_object
                if not getattr(record, "position_valid", False) and getattr(previous, "position_valid", False):
                    record.position_valid = previous.position_valid
                    record.position_x = previous.position_x
                    record.position_y = previous.position_y
                    record.position_z = previous.position_z
                if record.identity_error == 0:
                    record.identity_error = previous.identity_error
                if record.query is None:
                    record.query = previous.query
                if not record.probe_error:
                    record.probe_error = previous.probe_error

        self.clients = records
        self.clients_by_pid = {record.pid: record for record in records}
        live_pids = set(self.clients_by_pid.keys())
        self.auto_loaded_character_keys = {
            pid: key
            for pid, key in self.auto_loaded_character_keys.items()
            if pid in live_pids
        }
        self.default_started_scripts = {
            key
            for key in self.default_started_scripts
            if key[0] in live_pids
        }
        for record in records:
            self._auto_load_character_defaults(record)
        for record in records:
            if record.injected:
                self.script_manager.enable_overlay_controls(record)
            else:
                self.script_manager.disable_overlay_controls(record.pid)
            self.script_manager.sync_client(record)
            if record.injected:
                self._ensure_default_scripts_running(record)

        for pid in list(self.script_manager.hosts.keys()):
            if pid not in live_pids:
                self.script_manager.stop_all_for_client(pid)

        self.client_tree.delete(*self.client_tree.get_children())
        for record in records:
            self.client_tree.insert(
                "",
                "end",
                iid=str(record.pid),
                values=(
                    record.ordinal,
                    record.pid,
                    "Yes" if record.injected else "No",
                    record.character_name or "-",
                    record.window_title or "-",
                    record.created_text,
                    self.script_manager.running_script_count(record.pid),
                ),
            )

        if old_selected in self.clients_by_pid:
            self.selected_pid = old_selected
        elif records:
            self.selected_pid = records[0].pid
        else:
            self.selected_pid = None

        if self.selected_pid is not None:
            self.client_tree.selection_set(str(self.selected_pid))
            self.client_tree.focus(str(self.selected_pid))
        self.refresh_selected_client_ui()

    def _ensure_default_scripts_running(self, record):
        if record is None or not getattr(record, "injected", False):
            return

        for script_id in DEFAULT_AUTO_START_SCRIPT_IDS:
            definition = self.script_manager.registry.get(script_id)
            if definition is None:
                continue
            if not self.get_script_autostart(record.pid, script_id):
                continue

            key = (record.pid, script_id)
            if key in self.default_started_scripts:
                continue

            if self.script_manager.get_state(record.pid, script_id).get("running"):
                self.default_started_scripts.add(key)
                continue

            try:
                self.script_manager.start_script(record, script_id, self.get_script_config(record.pid, script_id))
                self.default_started_scripts.add(key)
                self.log(f"{record.display_name}: started {definition.name} by default", "info")
            except Exception as exc:
                self.log(f"{record.display_name}: default {definition.name} start failed: {exc}", "error")

    def refresh_client_tree_rows(self):
        for record in self.clients:
            if self.client_tree.exists(str(record.pid)):
                self.client_tree.set(str(record.pid), "scripts", self.script_manager.running_script_count(record.pid))

    def on_client_selected(self, _event=None):
        old_selected = self.selected_pid
        self.persist_loaded_configs(old_selected)
        selection = self.client_tree.selection()
        if not selection:
            self.selected_pid = None
        else:
            self.selected_pid = int(selection[0])
        self.refresh_selected_client_ui()

    def selected_client(self):
        if self.selected_pid is None:
            return None
        return self.clients_by_pid.get(self.selected_pid)

    def refresh_selected_client_ui(self):
        client = self.selected_client()
        if client is None:
            self.selected_name_var.set("No client selected")
            self.selected_details_var.set("Select an NWN client to see details.")
            self.refresh_target_analysis_panel()
            for row in self.script_rows.values():
                row.set_enabled(False)
            return

        self.selected_name_var.set(f"#{client.ordinal} {client.display_name}")
        position_text = (
            f"({client.position_x:.2f}, {client.position_y:.2f}, {client.position_z:.2f})"
            if getattr(client, "position_valid", False)
            else "<unknown>"
        )
        detail_lines = [
            f"PID: {client.pid}    Injected: {'Yes' if client.injected else 'No'}    Scripts: {self.script_manager.running_script_count(client.pid)}",
            f"Character: {client.character_name or '<unknown>'}    Player Object: 0x{client.player_object:08X}    Position: {position_text}",
            f"Window: {client.window_title or '<untitled>'}",
            f"Class: {client.window_class or '<unknown>'}    HWND: 0x{client.hwnd:08X}    Thread: {client.thread_id}",
            f"Started: {client.created_text}    Identity Error: {client.identity_error}",
        ]
        if client.query:
            detail_lines.append(
                "Quickbar: "
                f"panel=0x{int(client.query.get('quickbar_this', 0)):08X} "
                f"page={int(client.query.get('quickbar_page', -1))} "
                f"slot={int(client.query.get('quickbar_slot', -1))} "
                f"slotType={int(client.query.get('quickbar_slot_type', 0))}"
            )
        if client.probe_error and not client.injected:
            detail_lines.append(f"Probe: {client.probe_error}")
        self.selected_details_var.set("\n".join(detail_lines))

        for row in self.script_rows.values():
            row.load_for_client(client.pid)
        self.refresh_target_analysis_panel()

    def get_script_config(self, client_pid, script_id):
        key = (client_pid, script_id)
        if key not in self.script_configs:
            self.script_configs[key] = self.script_manager.default_config(script_id)
        config = self._clean_script_config(script_id, self.script_configs[key])
        self.script_configs[key] = config
        return dict(config)

    def set_script_config(self, client_pid, script_id, config):
        cleaned = self._clean_script_config(script_id, config)
        self.script_configs[(client_pid, script_id)] = cleaned
        if script_id == COORDINATE_FOLLOW_SCRIPT_ID and self._coordinate_follow_config_is_lead(cleaned):
            self._enforce_coordinate_follow_lead_config(client_pid)
        self._save_character_defaults_for_client(client_pid)

    def apply_script_config_change(self, client_pid, script_id, config):
        cleaned = self._clean_script_config(script_id, config)
        current = self.get_script_config(client_pid, script_id)
        if cleaned == current:
            return False

        client = self.clients_by_pid.get(client_pid)
        state = self.script_manager.get_state(client_pid, script_id)
        was_running = bool(state.get("running"))
        runtime_role = None
        if script_id == COORDINATE_FOLLOW_SCRIPT_ID and was_running:
            runtime_role = str((state.get("details") or {}).get("role") or "").strip()

        self.set_script_config(client_pid, script_id, cleaned)
        if client is None or not getattr(client, "injected", False) or not was_running:
            return True

        restart_config = dict(cleaned)
        if script_id == COORDINATE_FOLLOW_SCRIPT_ID and runtime_role:
            restart_config["role"] = runtime_role
        toggle_key = (client_pid, script_id)
        if toggle_key in self.script_toggles_in_progress:
            self.log(f"{client.display_name}: {script_id} settings saved; restart skipped because the script is already changing state", "info")
            return True
        self.toggle_script_for_client(
            client,
            script_id,
            restart_config,
            source="GUI",
            force_start=True,
            persist_config=False,
        )
        return True

    def set_coordinate_follow_saved_lead(self, client_pid, enabled, config=None, enable_saved=False):
        if config is None:
            config = self.get_script_config(client_pid, COORDINATE_FOLLOW_SCRIPT_ID)
        else:
            config = self._clean_script_config(COORDINATE_FOLLOW_SCRIPT_ID, config)
        config["role"] = CoordinateFollowScript.ROLE_LEAD if enabled else CoordinateFollowScript.ROLE_FOLLOWER
        if enable_saved and enabled:
            self.script_autostart[(client_pid, COORDINATE_FOLLOW_SCRIPT_ID)] = True
        self.set_script_config(client_pid, COORDINATE_FOLLOW_SCRIPT_ID, config)
        return dict(config)

    def _coordinate_follow_config_is_lead(self, config):
        role = str((config or {}).get("role", "") or "").strip().lower()
        return role == CoordinateFollowScript.ROLE_LEAD.lower()

    def _enforce_coordinate_follow_lead_config(self, lead_pid):
        changed_pids = set()
        lead_pid = int(lead_pid)

        for (pid, script_id), config in list(self.script_configs.items()):
            if script_id != COORDINATE_FOLLOW_SCRIPT_ID or int(pid) == lead_pid:
                continue
            if not self._coordinate_follow_config_is_lead(config):
                continue
            demoted = dict(config)
            demoted["role"] = CoordinateFollowScript.ROLE_FOLLOWER
            self.script_configs[(pid, script_id)] = self._clean_script_config(script_id, demoted)
            changed_pids.add(pid)

        basic_key = (lead_pid, BASIC_FUNCTIONS_SCRIPT_ID)
        basic_config = self.get_script_config(lead_pid, BASIC_FUNCTIONS_SCRIPT_ID)
        if not bool(basic_config.get("disable_follow", False)):
            basic_config["disable_follow"] = True
            self.script_configs[basic_key] = self._clean_script_config(BASIC_FUNCTIONS_SCRIPT_ID, basic_config)
            changed_pids.add(lead_pid)

        for pid in changed_pids:
            self._save_character_defaults_for_client(pid)

    def _prepare_coordinate_follow_lead_runtime(self, lead_pid):
        lead_pid = int(lead_pid)
        self._enforce_coordinate_follow_lead_config(lead_pid)
        messages = []
        for pid in list(self.script_manager.hosts.keys()):
            if int(pid) == lead_pid:
                continue
            state = self.script_manager.get_state(pid, COORDINATE_FOLLOW_SCRIPT_ID)
            details = state.get("details") or {}
            if not state.get("running") or str(details.get("role") or "").lower() != CoordinateFollowScript.ROLE_LEAD.lower():
                continue

            client = self.clients_by_pid.get(pid)
            display_name = client.display_name if client is not None else f"pid={pid}"
            follower_config = self.get_script_config(pid, COORDINATE_FOLLOW_SCRIPT_ID)
            follower_config["role"] = CoordinateFollowScript.ROLE_FOLLOWER
            self.script_configs[(pid, COORDINATE_FOLLOW_SCRIPT_ID)] = self._clean_script_config(
                COORDINATE_FOLLOW_SCRIPT_ID,
                follower_config,
            )
            self._save_character_defaults_for_client(pid)
            self.script_manager.stop_script(pid, COORDINATE_FOLLOW_SCRIPT_ID)
            if client is not None and getattr(client, "injected", False):
                self.script_manager.start_script(client, COORDINATE_FOLLOW_SCRIPT_ID, follower_config)
                messages.append(f"{display_name}: demoted to Coordinate Follow follower")
            else:
                messages.append(f"{display_name}: stopped old Coordinate Follow lead")
        return messages

    def get_script_autostart(self, client_pid, script_id):
        key = (client_pid, script_id)
        if key in self.script_autostart:
            return bool(self.script_autostart[key])
        return self._default_script_autostart(script_id)

    def set_script_autostart(self, client_pid, script_id, enabled):
        key = (client_pid, script_id)
        enabled = bool(enabled)
        changed = self.get_script_autostart(client_pid, script_id) != enabled
        self.script_autostart[key] = enabled
        if changed:
            self._save_character_defaults_for_client(client_pid)
        return changed

    def get_script_autostart_ids(self, client_pid):
        return [
            script_id
            for script_id in self.script_manager.registry.keys()
            if self.get_script_autostart(client_pid, script_id)
        ]

    def inject_next_async(self):
        def action():
            records = runtime.discover_clients(process_name=self.args.process_name)
            if not records:
                raise RuntimeError("No nwmain.exe clients are running.")
            target = runtime.find_uninjected_client(records)
            if target is None:
                return "All discovered NWN clients are already injected."
            base, func = runtime.inject_client(
                target,
                self.args.dll,
                self.args.export,
                python_path=self.args.inject_python,
            )
            return f"Injected client #{target.ordinal} pid={target.pid} base=0x{base:08X} init=0x{func:08X}"

        self.run_background("Inject Next", action, refresh_after=True)

    def inject_all_async(self):
        def action():
            records = runtime.discover_clients(process_name=self.args.process_name)
            if not records:
                raise RuntimeError("No nwmain.exe clients are running.")
            targets = [record for record in records if not record.injected]
            if not targets:
                return "All discovered NWN clients are already injected."
            messages = []
            for target in targets:
                base, func = runtime.inject_client(
                    target,
                    self.args.dll,
                    self.args.export,
                    python_path=self.args.inject_python,
                )
                messages.append(f"#{target.ordinal} pid={target.pid} base=0x{base:08X} init=0x{func:08X}")
            return "Injected clients: " + "; ".join(messages)

        self.run_background("Inject All", action, refresh_after=True)

    def start_saved_scripts_all_async(self):
        self.persist_loaded_configs(self.selected_pid)
        clients = [record for record in self.clients if record.injected]
        if not clients:
            messagebox.showwarning("HGCC", "No injected clients are available.")
            return

        def action():
            started = []
            already_running = 0
            skipped_clients = []
            errors = []
            coordinate_lead_pid = self._resolve_saved_coordinate_follow_lead(clients)

            for client in clients:
                script_ids = self.get_script_autostart_ids(client.pid)
                if not script_ids:
                    skipped_clients.append(client.display_name)
                    continue

                for script_id in script_ids:
                    definition = self.script_manager.registry.get(script_id)
                    if definition is None:
                        continue
                    state = self.script_manager.get_state(client.pid, script_id)
                    if state.get("running"):
                        already_running += 1
                        continue
                    try:
                        config = self.get_script_config(client.pid, script_id)
                        if script_id == COORDINATE_FOLLOW_SCRIPT_ID:
                            config = dict(config)
                            if client.pid == coordinate_lead_pid:
                                config["role"] = CoordinateFollowScript.ROLE_LEAD
                                self._prepare_coordinate_follow_lead_runtime(client.pid)
                            elif self._coordinate_follow_config_is_lead(config):
                                config["role"] = CoordinateFollowScript.ROLE_FOLLOWER
                                self.set_script_config(client.pid, script_id, config)
                        self.script_manager.start_script(client, script_id, config)
                        started.append(f"{client.display_name}: {definition.name}")
                    except Exception as exc:
                        errors.append(f"{client.display_name}: {definition.name}: {exc}")

            parts = []
            if started:
                parts.append("Started " + ", ".join(started))
            if already_running:
                parts.append(f"{already_running} already running")
            if skipped_clients:
                parts.append("No saved scripts for " + ", ".join(skipped_clients))
            if errors:
                parts.append("Errors: " + " | ".join(errors))
            return "; ".join(parts) if parts else "No saved scripts were selected."

        self.run_background("Start Saved Scripts", action)

    def _resolve_saved_coordinate_follow_lead(self, clients):
        lead_pid = None
        for client in clients:
            if COORDINATE_FOLLOW_SCRIPT_ID not in self.get_script_autostart_ids(client.pid):
                continue
            config = self.get_script_config(client.pid, COORDINATE_FOLLOW_SCRIPT_ID)
            if not self._coordinate_follow_config_is_lead(config):
                continue
            if lead_pid is None:
                lead_pid = client.pid
                continue
            config = dict(config)
            config["role"] = CoordinateFollowScript.ROLE_FOLLOWER
            self.set_script_config(client.pid, COORDINATE_FOLLOW_SCRIPT_ID, config)
        return lead_pid

    def stop_all_scripts_async(self):
        self.persist_loaded_configs(self.selected_pid)

        def action():
            stopped = []
            for pid, host in list(self.script_manager.hosts.items()):
                client = self.clients_by_pid.get(pid)
                display_name = client.display_name if client is not None else f"pid={pid}"
                for script_id in host.running_script_ids():
                    definition = self.script_manager.registry.get(script_id)
                    script_name = definition.name if definition is not None else script_id
                    self.script_manager.stop_script(pid, script_id)
                    stopped.append(f"{display_name}: {script_name}")

            if not stopped:
                return "No running scripts to stop."
            return "Stopped " + ", ".join(stopped)

        self.run_background("Stop All Scripts", action)

    def assign_auto_attack_lead_async(self):
        lead = self.selected_client()
        if lead is None:
            messagebox.showwarning("HGCC", "Select the lead client first.")
            return

        lead_name = str(getattr(lead, "character_name", "") or getattr(lead, "display_name", "") or "").strip()
        if not lead_name:
            messagebox.showwarning("HGCC", "The selected client does not have a known character name yet.")
            return

        followers = [
            record
            for record in self.clients
            if getattr(record, "injected", False) and getattr(record, "pid", None) != lead.pid
        ]
        if not followers and not getattr(lead, "injected", False):
            messagebox.showwarning("HGCC", "No injected clients are available for lead assignment.")
            return

        self.persist_loaded_configs(self.selected_pid)

        def action():
            parts = []
            errors = []
            if getattr(lead, "injected", False):
                state = self.script_manager.get_state(lead.pid, "auto_attack")
                if state.get("running"):
                    self.script_manager.stop_script(lead.pid, "auto_attack")
                    parts.append(f"{lead.display_name}: Auto Attack off")
                else:
                    parts.append(f"{lead.display_name}: Auto Attack already off")

            assigned = []
            for follower in followers:
                try:
                    role_result = runtime.send_chat(follower, "!role lead", 2)
                    target_result = runtime.send_chat(follower, f'/tell "{lead_name}" !target', 2)
                    success = bool(role_result.get("success") and target_result.get("success"))
                except Exception as exc:
                    errors.append(f"{follower.display_name}: {exc}")
                    continue

                if success:
                    assigned.append(follower.display_name)
                else:
                    errors.append(
                        (
                            f"{follower.display_name}: role success={role_result.get('success')} "
                            f"rc={role_result.get('rc')} err={role_result.get('err')}; "
                            f"target success={target_result.get('success')} "
                            f"rc={target_result.get('rc')} err={target_result.get('err')}"
                        )
                    )

            if assigned:
                parts.append(f"assigned lead {lead_name} for " + ", ".join(assigned))
            elif followers:
                parts.append(f"no followers assigned to {lead_name}")
            else:
                parts.append("no other injected clients")

            if errors:
                parts.append("Errors: " + " | ".join(errors))
            return "; ".join(parts)

        self.run_background("Assign Auto Attack Lead", action)

    def trigger_slot_async(self, slot, page=0, bank_label="Base"):
        client = self.selected_client()
        if client is None or not client.injected:
            messagebox.showwarning("HGCC", "Select an injected client first.")
            return

        def action():
            result = runtime.trigger_slot(client, slot, page=page)
            if page == 0:
                trigger_name = f"F{slot}"
            else:
                trigger_name = f"{bank_label}+F{slot}"
            return (
                f"{client.display_name}: {trigger_name} "
                f"success={result['success']} rc={result['rc']} aux={result['aux_rc']} "
                f"path={result['path']} err={result['err']} page={result['page']}"
            )

        self.run_background(f"Trigger {bank_label} Slot {slot}", action)

    def send_chat_async(self):
        client = self.selected_client()
        text = self.chat_entry_var.get().strip()
        if client is None or not client.injected:
            messagebox.showwarning("HGCC", "Select an injected client first.")
            return
        if not text:
            messagebox.showwarning("HGCC", "Enter some chat text first.")
            return

        def action():
            result = runtime.send_chat(client, text, 2)
            return (
                f"{client.display_name}: chat-send success={result['success']} "
                f"mode={result['mode']} rc={result['rc']} err={result['err']}"
            )

        self.run_background("Send Chat", action)
        self.chat_entry_var.set("")

    def toggle_script(self, script_id, config):
        client = self.selected_client()
        if client is None or not client.injected:
            messagebox.showwarning("HGCC", "Select an injected client first.")
            return

        self.toggle_script_for_client(client, script_id, config, source="GUI")

    def start_script(self, script_id, config):
        client = self.selected_client()
        if client is None or not client.injected:
            messagebox.showwarning("HGCC", "Select an injected client first.")
            return

        self.toggle_script_for_client(client, script_id, config, source="GUI", force_start=True)

    def toggle_script_for_client(self, client, script_id, config, source="GUI", force_start=False, persist_config=True):
        if client is None or not getattr(client, "injected", False):
            self.log(f"{source}: select an injected client first.", "error")
            return

        if persist_config:
            self.set_script_config(client.pid, script_id, config)
        toggle_key = (client.pid, script_id)
        if toggle_key in self.script_toggles_in_progress:
            self.log(f"{client.display_name}: {script_id} is already changing state", "info")
            if source == "Overlay":
                self._send_ingame_echo(client, "HGCC: script is already changing")
            return

        state = self.script_manager.get_state(client.pid, script_id)
        restarting = bool(force_start and state["running"])
        starting = bool(force_start or not state["running"])
        busy_text = "Restarting..." if restarting else ("Starting..." if starting else "Stopping...")
        self.script_toggles_in_progress[toggle_key] = busy_text

        row = self.script_rows.get(script_id) if client.pid == self.selected_pid else None
        if row is not None:
            row.status_var.set(busy_text)
            row.toggle_button.configure(state="disabled", text=busy_text)
            if hasattr(row, "coordinate_lead_button"):
                row.coordinate_lead_button.configure(state="disabled")
                row.coordinate_follower_button.configure(state="disabled")

        def action():
            try:
                script_name = self.script_manager.registry[script_id].name
                current_state = self.script_manager.get_state(client.pid, script_id)
                if current_state["running"]:
                    if not force_start:
                        self.script_manager.stop_script(client.pid, script_id)
                        if source == "Overlay":
                            self._send_ingame_echo(client, f"HGCC: {script_name} off")
                        return f"{client.display_name}: stopped {script_id}"
                    self.script_manager.stop_script(client.pid, script_id)

                lead_messages = []
                if script_id == COORDINATE_FOLLOW_SCRIPT_ID and self._coordinate_follow_config_is_lead(config):
                    lead_messages = self._prepare_coordinate_follow_lead_runtime(client.pid)

                self.script_manager.start_script(client, script_id, config)
                if source == "Overlay":
                    self._send_ingame_echo(client, f"HGCC: {script_name} on")
                message = f"{client.display_name}: {'restarted' if restarting else 'started'} {script_id}"
                if lead_messages:
                    message += "; " + "; ".join(lead_messages)
                return message
            finally:
                self.enqueue_event({
                    "type": "script-toggle-finished",
                    "client_pid": client.pid,
                    "script_id": script_id,
                })

        self.run_background(f"Toggle {script_id}", action)

    def _send_ingame_echo(self, client, message):
        text = str(message or "").strip()
        if not text:
            return

        def worker():
            try:
                runtime.send_chat(client, f"!echo {text}", 2)
            except Exception as exc:
                self.enqueue_event({
                    "type": "log",
                    "level": "error",
                    "message": f"{client.display_name}: !echo feedback failed: {exc}",
                })

        threading.Thread(target=worker, name=f"SimKeysEcho-{client.pid}", daemon=True).start()

    def on_close(self):
        self.closing = True
        if self.scroll_refresh_after_id is not None:
            try:
                self.root.after_cancel(self.scroll_refresh_after_id)
            except tk.TclError:
                pass
            self.scroll_refresh_after_id = None
        try:
            self.persist_loaded_configs(self.selected_pid)
            self.script_manager.stop_all()
        finally:
            self.root.destroy()


def build_parser():
    parser = argparse.ArgumentParser(description="Desktop HGCC control client.")
    parser.add_argument("--process-name", default="nwmain.exe", help="Process image name to discover. Default: nwmain.exe")
    parser.add_argument("--dll", default=runtime.default_dll_path())
    parser.add_argument("--export", default="InitSimKeys")
    parser.add_argument("--inject-python", help="Optional alternate Python interpreter to use for injection.")
    parser.add_argument("--refresh-ms", type=int, default=2500, help="Auto-refresh interval in milliseconds. Default: 2500")
    return parser


def main():
    args = build_parser().parse_args()
    root = tk.Tk()
    SimKeysDesktopApp(root, args)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
