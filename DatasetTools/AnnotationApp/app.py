"""Tk desktop reviewer for extracted clips, original PTS and temporal boundaries."""

from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import time
import tkinter as tk
from tkinter import messagebox, ttk

import cv2
from PIL import Image, ImageTk

from DatasetTools.common import load_jsonl
from DatasetTools.annotation.contract import BOUNDARIES, new_annotation
from DatasetTools.annotation.journal import copy_semantic_labels, latest_annotations, save_revision
from DatasetTools.annotation.taxonomy import (ATTACK_TYPES, DIRECTIONS, IMPACT_EVIDENCE,
                                              MOVEMENTS, NEGATIVE_REASONS, PHASES, STATES)


class AnnotationApp:
    def __init__(self, window: tk.Tk, clips_path: Path, journal: Path, reviewer: str,
                 review_queue: Path | None = None):
        self.window, self.journal, self.reviewer = window, journal, reviewer
        self.clips = load_jsonl(clips_path)
        self.queue = {row["clip_id"]: row for row in load_jsonl(review_queue)} if review_queue else {}
        if review_queue:
            self.clips = sorted((clip for clip in self.clips if clip["clip_id"] in self.queue),
                                key=lambda clip: -self.queue[clip["clip_id"]].get("priority", 0))
        if not self.clips:
            raise ValueError("No extracted clips to review. Run the clip miner first.")
        self.cap = None
        self.clip = self.clips[0]
        self.mapping: list[dict] = []
        self.draft: dict = {}
        self.current = -1
        self.playing = False
        self.next_due = 0.0
        self.current_bgr = None
        self.last_draft = None
        self.photo = None
        self.drawing_roi = None
        self.display_rect = (0, 0, 1, 1)
        self.slider_guard = False
        self.window.title("SekiroVisionAI — Temporal Annotation Reviewer")
        self.window.geometry("1320x900")
        self.window.minsize(1080, 780)
        self.window.protocol("WM_DELETE_WINDOW", self.close)
        self._build_widgets()
        self._bind_keys()
        self.load_clip(0)
        self.window.after(15, self._tick)

    def _build_widgets(self):
        top = ttk.Frame(self.window, padding=8)
        top.pack(fill="both", expand=True)
        top.columnconfigure(1, weight=1)
        top.rowconfigure(0, weight=1)
        left = ttk.Frame(top)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        ttk.Label(left, text="Clips · proposals require review").pack(anchor="w")
        self.clip_list = tk.Listbox(left, width=31, exportselection=False)
        self.clip_list.pack(fill="both", expand=True)
        for clip in self.clips:
            marker = "TEST ONLY · " if clip.get("example_only") else ""
            self.clip_list.insert("end", marker + clip["clip_id"])
        self.clip_list.bind("<<ListboxSelect>>", self._select_clip)
        center = ttk.Frame(top)
        center.grid(row=0, column=1, sticky="nsew")
        center.columnconfigure(0, weight=1)
        center.rowconfigure(1, weight=1)
        self.title = ttk.Label(center, text="", wraplength=950)
        self.title.grid(row=0, column=0, sticky="ew")
        self.canvas = tk.Canvas(center, background="#101319", highlightthickness=0, height=430)
        self.canvas.grid(row=1, column=0, sticky="nsew", pady=6)
        self.canvas.bind("<Configure>", lambda event: self._render())
        self.canvas.bind("<ButtonPress-1>", self._roi_start)
        self.canvas.bind("<B1-Motion>", self._roi_move)
        self.canvas.bind("<ButtonRelease-1>", self._roi_end)
        self.frame_status = ttk.Label(center, text="")
        self.frame_status.grid(row=2, column=0, sticky="ew")
        self.slider = ttk.Scale(center, from_=0, to=1, command=self._slide)
        self.slider.grid(row=3, column=0, sticky="ew")
        playback = ttk.Frame(center)
        playback.grid(row=4, column=0, sticky="ew", pady=4)
        for text, command in (("◀ Frame", lambda: self.step(-1)), ("Play / Pause [Space]", self.toggle_play),
                              ("Frame ▶", lambda: self.step(1))):
            ttk.Button(playback, text=text, command=command).pack(side="left", padx=(0, 4))
        self.speed = tk.StringVar(value="0.5")
        ttk.Label(playback, text="Speed").pack(side="left", padx=(8, 2))
        ttk.Combobox(playback, textvariable=self.speed, values=("0.25", "0.5", "1", "2"),
                     state="readonly", width=5).pack(side="left")
        ttk.Button(playback, text="Motion suggestion", command=self.suggest_timeline).pack(side="left", padx=8)
        self.events = ttk.Combobox(center, state="readonly")
        self.events.grid(row=5, column=0, sticky="ew", pady=3)
        self.events.bind("<<ComboboxSelected>>", self._select_event)
        boundary_bar = ttk.Frame(center)
        boundary_bar.grid(row=6, column=0, sticky="ew", pady=3)
        labels = (("W · Windup", "windup_start"), ("A · Active", "active_start"),
                  ("I · Contact", "impact_frame"), ("R · Recovery", "recovery_start"),
                  ("E · Recovery end", "recovery_end"), ("D · Dodge", "dodge_start"))
        for text, field in labels:
            ttk.Button(boundary_bar, text=text, command=lambda key=field: self.mark(key)).pack(side="left", padx=2)
        self.boundary_text = ttk.Label(center, text="", wraplength=970)
        self.boundary_text.grid(row=7, column=0, sticky="ew")
        controls = ttk.Frame(center)
        controls.grid(row=8, column=0, sticky="ew", pady=6)
        self.variables: dict[str, tk.StringVar] = {}

        def combo(row, column, key, title, values, width=20):
            ttk.Label(controls, text=title).grid(row=row * 2, column=column, sticky="w", padx=4)
            variable = tk.StringVar()
            self.variables[key] = variable
            widget = ttk.Combobox(controls, textvariable=variable, values=values, state="readonly", width=width)
            widget.grid(row=row * 2 + 1, column=column, sticky="ew", padx=4, pady=(0, 4))
            if key == "impact_evidence":
                widget.bind("<<ComboboxSelected>>", self._impact_changed)

        combo(0, 0, "state", "State at anchor", STATES)
        combo(0, 1, "attack_type", "Attack class", ATTACK_TYPES)
        combo(0, 2, "impact_evidence", "Contact evidence (no-hit is censored)", IMPACT_EVIDENCE, 27)
        combo(0, 3, "threat_label", "Threat reviewed", ("UNKNOWN", "TRUE", "FALSE"), 13)
        combo(1, 0, "movement", "Movement", MOVEMENTS)
        combo(1, 1, "phase", "Phase at anchor", PHASES)
        combo(1, 2, "negative_reason", "Entire-clip negative reason", NEGATIVE_REASONS, 27)
        combo(1, 3, "dodge_direction", "Observed Dodge", DIRECTIONS, 13)
        details = ttk.Frame(center)
        details.grid(row=9, column=0, sticky="ew", pady=3)
        self.boss_phase = tk.StringVar(value="UNKNOWN")
        self.quality = tk.StringVar(value="0.8")
        ttk.Label(details, text="Boss phase").pack(side="left")
        ttk.Entry(details, textvariable=self.boss_phase, width=14).pack(side="left", padx=4)
        ttk.Label(details, text="Label confidence").pack(side="left", padx=(8, 0))
        ttk.Entry(details, textvariable=self.quality, width=6).pack(side="left", padx=4)
        ttk.Button(details, text="Counterfactual [start]", command=lambda: self.mark_interval(0)).pack(side="left", padx=4)
        ttk.Button(details, text="[end]", command=lambda: self.mark_interval(1)).pack(side="left")
        ttk.Button(details, text="Clear boundaries", command=self.clear_boundaries).pack(side="left", padx=8)
        self.notes = ttk.Entry(center)
        self.notes.grid(row=10, column=0, sticky="ew", pady=3)
        buttons = ttk.Frame(center)
        buttons.grid(row=11, column=0, sticky="ew", pady=5)
        for column in range(3):
            buttons.columnconfigure(column, weight=1)
        for index, (text, command) in enumerate((("Mark whole clip NON_THREAT", self.mark_negative),
                                                ("New event", self.new_event), ("Add combo strike", self.new_strike),
                                                ("Copy last labels", self.copy_last),
                                                ("Save proposal", lambda: self.save("proposed")),
                                                ("Accept reviewed [Ctrl+S]", lambda: self.save("reviewed")))):
            ttk.Button(buttons, text=text, command=command).grid(row=index // 3, column=index % 3,
                                                                 sticky="ew", padx=2, pady=2)
        self.message = ttk.Label(center, text="", wraplength=970)
        self.message.grid(row=12, column=0, sticky="ew")
        ttk.Label(top, text="←/→ frame · Space play · W/A/I/R/E/D boundaries · drag image for ROI · "
                            "No-contact clips never produce exact impact TTI.", wraplength=1260).grid(
                                row=1, column=0, columnspan=2, sticky="ew", pady=(6, 0))

    def _bind_keys(self):
        actions = {"Left": lambda: self.step(-1), "Right": lambda: self.step(1), "space": self.toggle_play,
                   "w": lambda: self.mark("windup_start"), "a": lambda: self.mark("active_start"),
                   "i": lambda: self.mark("impact_frame"), "r": lambda: self.mark("recovery_start"),
                   "e": lambda: self.mark("recovery_end"), "d": lambda: self.mark("dodge_start")}

        def on_key(event):
            if event.widget.winfo_class() in ("Entry", "TEntry", "Text", "TCombobox"):
                return
            command = actions.get(event.keysym)
            if command:
                command()
                return "break"

        self.window.bind("<KeyPress>", on_key)
        self.window.bind("<Control-s>", lambda event: self.save("reviewed"))

    def _select_clip(self, event=None):
        selection = self.clip_list.curselection()
        if selection and self.clips[selection[0]]["clip_id"] != self.clip["clip_id"]:
            self.load_clip(selection[0])

    def load_clip(self, index: int):
        self.playing = False
        if self.draft:
            self.last_draft = deepcopy(self._read_fields())
        if self.cap is not None:
            self.cap.release()
        self.clip = self.clips[index]
        self.mapping = load_jsonl(Path(self.clip["frame_map_path"]))
        self.cap = cv2.VideoCapture(self.clip["video_path"])
        if not self.cap.isOpened() or len(self.mapping) != self.clip["frame_count"]:
            raise ValueError("Clip media/frame map missing or inconsistent; run dataset validation.")
        self.clip_list.selection_clear(0, "end")
        self.clip_list.selection_set(index)
        self.title.configure(text=f"{self.clip['boss']} · {self.clip['clip_id']} · "
                                  f"{self.clip['proposal_reason']} ({self.clip['proposal_score']:.2f}, heuristic proposal)")
        self._refresh_events()
        self.draft = deepcopy(self.event_rows[-1]) if self.event_rows else new_annotation(self.clip, reviewer=self.reviewer)
        self._write_fields()
        self.current = -1
        self.slider.configure(to=len(self.mapping) - 1)
        self.show_frame(0)
        hint = self.queue.get(self.clip["clip_id"])
        self.message.configure(text=(f"Review queue: {hint['reason']} · model {hint['model_version']} · still unreviewed"
                                      if hint else "Review the full proposed interval; motion scores do not identify attacks."))

    def _refresh_events(self):
        self.event_rows = [row for row in latest_annotations(self.journal) if row["clip_id"] == self.clip["clip_id"]]
        self.events["values"] = [f"{row['strike_id'][:8]} · revision {row['revision']} · {row['annotation_status']} · {row['attack_type']}"
                                  for row in self.event_rows]
        if self.event_rows:
            self.events.current(len(self.event_rows) - 1)
        else:
            self.events.set("New proposed event")

    def _select_event(self, event=None):
        index = self.events.current()
        if index >= 0:
            self.draft = deepcopy(self.event_rows[index])
            self._write_fields()
            self._boundaries()

    def _read_fields(self):
        for key, variable in self.variables.items():
            value = variable.get()
            self.draft[key] = ({"TRUE": True, "FALSE": False}.get(value) if key == "threat_label"
                               else value or None if key == "negative_reason" else value)
        self.draft["boss_phase"] = self.boss_phase.get().strip() or "UNKNOWN"
        self.draft["notes"] = self.notes.get()
        self.draft["confidence"] = float(self.quality.get())
        self.draft["anchor_frame"] = self.mapping[max(0, self.current)]["source_frame"]
        return self.draft

    def _write_fields(self):
        for key, variable in self.variables.items():
            value = self.draft.get(key)
            if key == "threat_label":
                value = "TRUE" if value is True else "FALSE" if value is False else "UNKNOWN"
            variable.set(value or "")
        self.boss_phase.set(self.draft.get("boss_phase", "UNKNOWN"))
        self.quality.set(str(self.draft["confidence"]))
        self.notes.delete(0, "end")
        self.notes.insert(0, self.draft.get("notes", ""))
        self._boundaries()

    def _boundaries(self):
        text = " · ".join(f"{key}: {self.draft.get(key) if self.draft.get(key) is not None else 'unknown'}" for key in BOUNDARIES)
        interval = self.draft.get("estimated_contact_interval")
        self.boundary_text.configure(text=text + (f" · estimated interval: {interval}" if interval else ""))

    def _slide(self, value):
        if not self.slider_guard and self.mapping:
            self.playing = False
            self.show_frame(round(float(value)))

    def show_frame(self, index: int):
        index = max(0, min(index, len(self.mapping) - 1))
        if index == self.current:
            return
        if index != self.current + 1:
            self.cap.set(cv2.CAP_PROP_POS_FRAMES, index)
        okay, frame = self.cap.read()
        if not okay:
            self.playing = False
            self.message.configure(text=f"Decode failed at clip frame {index}; run dataset validation.")
            return
        self.current, self.current_bgr = index, frame
        self.slider_guard = True
        self.slider.set(index)
        self.slider_guard = False
        mapped = self.mapping[index]
        self.frame_status.configure(text=f"Clip frame {index}/{len(self.mapping)-1} · source frame {mapped['source_frame']} · "
                                          f"original PTS {mapped['source_pts_ms']:.3f} ms · "
                                          f"{'DUPLICATED observation' if mapped['duplicated'] else 'original observation'}")
        self._render()

    def _render(self):
        if self.current_bgr is None:
            return
        width, height = max(1, self.canvas.winfo_width()), max(1, self.canvas.winfo_height())
        rgb = Image.fromarray(cv2.cvtColor(self.current_bgr, cv2.COLOR_BGR2RGB))
        rgb.thumbnail((width, height), Image.Resampling.LANCZOS)
        x, y = (width - rgb.width) // 2, (height - rgb.height) // 2
        self.display_rect = (x, y, rgb.width, rgb.height)
        self.photo = ImageTk.PhotoImage(rgb)
        self.canvas.delete("all")
        self.canvas.create_image(x, y, image=self.photo, anchor="nw")
        roi = self.draft.get("roi", self.clip["roi"])
        self.canvas.create_rectangle(x + roi[0] * rgb.width, y + roi[1] * rgb.height,
                                      x + roi[2] * rgb.width, y + roi[3] * rgb.height,
                                      outline="#39d8a5", width=2)

    def _normalized_point(self, event):
        x, y, width, height = self.display_rect
        return max(0, min(1, (event.x - x) / width)), max(0, min(1, (event.y - y) / height))

    def _roi_start(self, event):
        self.canvas.focus_set()
        self.drawing_roi = self._normalized_point(event)

    def _roi_move(self, event):
        if self.drawing_roi is None:
            return
        x, y = self._normalized_point(event)
        start_x, start_y = self.drawing_roi
        self.draft["roi"] = [min(start_x, x), min(start_y, y), max(start_x, x), max(start_y, y)]
        self._render()

    def _roi_end(self, event):
        self._roi_move(event)
        self.drawing_roi = None
        roi = self.draft["roi"]
        if roi[2] - roi[0] < 0.08 or roi[3] - roi[1] < 0.08:
            self.draft["roi"] = self.clip["roi"][:]
            self._render()

    def step(self, delta: int):
        self.playing = False
        self.show_frame(self.current + delta)

    def toggle_play(self):
        self.playing = not self.playing
        self.next_due = time.perf_counter()

    def _tick(self):
        if self.playing and time.perf_counter() >= self.next_due:
            if self.current + 1 >= len(self.mapping):
                self.playing = False
            else:
                self.show_frame(self.current + 1)
                self.next_due = time.perf_counter() + self.clip["fps_den"] / self.clip["fps_num"] / float(self.speed.get())
        self.window.after(15, self._tick)

    def mark(self, field: str):
        self.playing = False
        self.draft[field] = self.mapping[self.current]["source_frame"]
        self.draft["scope"] = "EVENT"
        if field == "impact_frame":
            self.variables["impact_evidence"].set("OBSERVED_CONTACT")
            self.draft["impact_evidence"] = "OBSERVED_CONTACT"
            self.draft["estimated_contact_interval"] = None
        if field == "dodge_start":
            self.draft["dodge_timing_evidence"] = "VISUAL_ONSET"
        self._boundaries()

    def mark_interval(self, index: int):
        self.playing = False
        interval = self.draft.get("estimated_contact_interval") or [None, None]
        interval[index] = self.mapping[self.current]["source_frame"]
        self.draft["estimated_contact_interval"] = interval
        self.draft["impact_frame"] = None
        self.draft["impact_evidence"] = "ESTIMATED_COUNTERFACTUAL"
        self.variables["impact_evidence"].set("ESTIMATED_COUNTERFACTUAL")
        self._boundaries()

    def _impact_changed(self, event=None):
        evidence = self.variables["impact_evidence"].get()
        self.draft["impact_evidence"] = evidence
        if evidence != "OBSERVED_CONTACT":
            self.draft["impact_frame"] = None
        if evidence != "ESTIMATED_COUNTERFACTUAL":
            self.draft["estimated_contact_interval"] = None
        self._boundaries()

    def clear_boundaries(self):
        for field in BOUNDARIES:
            self.draft[field] = None
        self.draft.update({"estimated_contact_interval": None, "impact_evidence": "OUT_OF_CLIP",
                           "dodge_direction": "UNKNOWN", "dodge_timing_evidence": "NOT_OBSERVED"})
        self.variables["impact_evidence"].set("OUT_OF_CLIP")
        self.variables["dodge_direction"].set("UNKNOWN")
        self._boundaries()

    def mark_negative(self):
        reason = self.variables["negative_reason"].get()
        if not reason:
            self.message.configure(text="Choose the reviewed negative reason, then mark the whole clip NON_THREAT.")
            return
        self.clear_boundaries()
        self.draft.update({"scope": "ENTIRE_CLIP_NON_THREAT", "threat_label": False,
                           "negative_reason": reason, "state": "NON_THREAT", "phase": "UNKNOWN_PHASE",
                           "impact_evidence": "NO_CONTACT"})
        self.variables["state"].set("NON_THREAT")
        self.variables["phase"].set("UNKNOWN_PHASE")
        self.variables["threat_label"].set("FALSE")
        self.variables["impact_evidence"].set("NO_CONTACT")
        self.message.configure(text="Whole-clip negative proposal set. Accept only after watching the full clip.")

    def new_event(self):
        self.last_draft = deepcopy(self._read_fields())
        self.draft = new_annotation(self.clip, reviewer=self.reviewer)
        self.events.set("New proposed event")
        self._write_fields()

    def new_strike(self):
        previous = deepcopy(self._read_fields())
        self.draft = copy_semantic_labels(previous, self.clip, self.reviewer)
        self.draft["event_id"] = previous["event_id"]
        self.events.set("New strike in current combo (unreviewed)")
        self._write_fields()

    def copy_last(self):
        if self.last_draft is None:
            self.message.configure(text="Review another clip/event first; copy reuses taxonomy and ROI only.")
            return
        self.draft = copy_semantic_labels(self.last_draft, self.clip, self.reviewer)
        self._write_fields()
        self.message.configure(text="Copied semantic labels only. Contact, Dodge times, and review status were cleared.")

    def suggest_timeline(self):
        path = Path(self.clip["video_path"]).parent / f"{self.clip['source_id']}.motion.jsonl"
        signals = [row for row in load_jsonl(path) if row.get("valid") and not row.get("camera_only")
                   and self.clip["start_source_frame"] <= row["source_frame"] < self.clip["end_source_frame"]]
        if not signals:
            self.message.configure(text="No reliable local-motion signal in this clip; no attack suggestion was created.")
            return
        peak = max(signals, key=lambda row: row["score"])
        if peak["score"] < 0.30:
            self.message.configure(text="Low-motion clip: review as an idle/camera/other negative if appropriate.")
            return
        before = [row for row in signals if row["source_frame"] < peak["source_frame"] and row["score"] >= peak["score"] * 0.4]
        after = [row for row in signals if row["source_frame"] > peak["source_frame"] and row["score"] < peak["score"] * 0.4]
        self.clear_boundaries()
        self.draft["windup_start"] = before[0]["source_frame"] if before else peak["source_frame"]
        self.draft["active_start"] = peak["source_frame"]
        self.draft["recovery_start"] = after[0]["source_frame"] if after else None
        self.draft["scope"] = "EVENT"
        self.variables["attack_type"].set("UNKNOWN_ATTACK")
        self.variables["threat_label"].set("UNKNOWN")
        self.variables["phase"].set("ATTACK_WINDUP")
        self.variables["state"].set("ATTACK_WINDUP")
        self._boundaries()
        index = min(range(len(self.mapping)), key=lambda i: abs(self.mapping[i]["source_pts_ms"] - peak["source_pts_ms"]))
        self.show_frame(index)
        self.message.configure(text="Motion-only boundary PROPOSAL. Review windup/active/recovery and class; impact/TTI remains unknown.")

    def save(self, status: str):
        try:
            self.playing = False
            draft = self._read_fields()
            self.draft = save_revision(self.journal, draft, self.clip, reviewer=self.reviewer, status=status)
            self.last_draft = deepcopy(self.draft)
            self._refresh_events()
            self.message.configure(text=f"Saved {status} revision {self.draft['revision']} · "
                                          f"{self.draft['annotation_id']} · journal {self.journal}")
        except (ValueError, OSError) as error:
            messagebox.showerror("Annotation needs correction", str(error), parent=self.window)

    def close(self):
        self.playing = False
        if self.cap is not None:
            self.cap.release()
        self.window.destroy()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clips", type=Path, default=Path("data/clips.jsonl"))
    parser.add_argument("--annotations", type=Path, default=Path("data/annotations.jsonl"))
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--review-queue", type=Path)
    args = parser.parse_args()
    if not args.reviewer.strip():
        parser.error("A reviewer identity is required; accepted annotations retain their reviewer.")
    window = tk.Tk()
    try:
        AnnotationApp(window, args.clips, args.annotations, args.reviewer, args.review_queue)
    except (ValueError, OSError) as error:
        window.destroy()
        parser.error(str(error))
    window.mainloop()


if __name__ == "__main__":
    main()
