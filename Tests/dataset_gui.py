"""Bounded real Tk smoke under Xvfb. All media/labels are SYNTHETIC TEST ONLY."""

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import time
import unittest
from unittest.mock import patch

from DatasetTools.common import load_jsonl, write_jsonl

GUI_AVAILABLE = (bool(os.environ.get("DISPLAY") or os.name == "nt") and
                 all(importlib.util.find_spec(module) is not None for module in ("cv2", "numpy", "tkinter", "PIL")) and
                 bool(shutil.which("ffmpeg") and shutil.which("ffprobe")))


@unittest.skipUnless(GUI_AVAILABLE, "Run under xvfb-run with Tk/Pillow/OpenCV/FFmpeg installed")
class AnnotationGuiSmoke(unittest.TestCase):
    def test_real_window_playback_marks_and_review_journal(self):
        import cv2
        import numpy as np
        import tkinter as tk

        from DatasetTools.AnnotationApp.app import AnnotationApp
        from DatasetTools.annotation.journal import latest_annotations
        from DatasetTools.clip_miner.mine import Candidate, extract_candidates
        from DatasetTools.downloader.ingest import ingest

        with tempfile.TemporaryDirectory(prefix="svai-gui-SYNTHETIC-TEST-ONLY-") as directory:
            folder = Path(directory)
            original = folder / "SYNTHETIC_TEST_ONLY.avi"
            writer = cv2.VideoWriter(str(original), cv2.VideoWriter_fourcc(*"MJPG"), 30, (256, 144))
            self.assertTrue(writer.isOpened())
            for frame_index in range(96):
                frame = np.full((144, 256, 3), (45, 65, 85), dtype=np.uint8)
                cv2.rectangle(frame, (20 + frame_index, 36), (65 + frame_index, 105), (30, 160, 240), -1)
                writer.write(frame)
            writer.release()
            source = ingest(input_path=str(original), source_id="SYNTHETIC_GUI_ONLY", creator="SYNTHETIC TEST ONLY",
                            session_id="SYNTHETIC_GUI_SESSION", boss="SYNTHETIC SHAPES — NOT A BOSS",
                            usage_basis="SYNTHETIC_TEST", usage_evidence="Generated GUI test fixture", root=folder / "data")
            clips = extract_candidates(source, [Candidate(0, 3000, 1350, "SYNTHETIC TEST ONLY", 0.8)],
                                       folder / "clips", max_width=256)
            manifest, journal = folder / "clips.jsonl", folder / "annotations.jsonl"
            write_jsonl(manifest, clips)
            mapping = load_jsonl(Path(clips[0]["frame_map_path"]))
            write_jsonl(folder / "clips" / "SYNTHETIC_GUI_ONLY.motion.jsonl", [
                {"valid": True, "camera_only": False, "source_frame": mapping[index]["source_frame"],
                 "source_pts_ms": mapping[index]["source_pts_ms"], "score": score}
                for index, score in ((20, 0.35), (40, 0.8), (65, 0.1))])
            window = tk.Tk()
            app = None
            try:
                app = AnnotationApp(window, manifest, journal, "SYNTHETIC TEST ONLY")
                window.geometry("1280x900")
                window.update_idletasks()
                window.update()
                self.assertEqual(0, app.current)
                self.assertIsNotNone(app.photo)
                self.assertGreater(app.photo.width(), 10)
                app.step(1)
                self.assertEqual(1, app.current)
                app.show_frame(30)
                self.assertEqual(30, app.current)
                app.toggle_play()
                time.sleep(0.05)
                window.update()
                self.assertGreater(app.current, 30)
                app.toggle_play()
                self.assertFalse(app.playing)

                app.suggest_timeline()
                self.assertIsNotNone(app.draft["windup_start"])
                self.assertIsNone(app.draft["impact_frame"])
                self.assertEqual("proposed", app.draft["annotation_status"])
                for index, field in ((25, "windup_start"), (40, "active_start"),
                                     (56, "recovery_start"), (72, "recovery_end")):
                    app.show_frame(index)
                    app.mark(field)
                app.variables["impact_evidence"].set("NO_CONTACT")
                app._impact_changed()
                app.variables["attack_type"].set("HORIZONTAL_SLASH")
                app.variables["state"].set("ACTIVE_ATTACK")
                app.variables["phase"].set("ACTIVE_ATTACK")
                app.variables["threat_label"].set("TRUE")
                app.show_frame(45)

                def fail_modal(*arguments, **kwargs):
                    raise AssertionError(f"GUI produced an unexpected validation modal: {arguments}")

                with patch("DatasetTools.AnnotationApp.app.messagebox.showerror", side_effect=fail_modal):
                    app.save("proposed")
                    app.save("reviewed")
                revisions = load_jsonl(journal)
                self.assertEqual(["proposed", "reviewed"], [row["annotation_status"] for row in revisions])
                reviewed = latest_annotations(journal)[0]
                self.assertEqual(2, reviewed["revision"])
                self.assertTrue(reviewed["example_only"])
                self.assertEqual("NO_CONTACT", reviewed["impact_evidence"])
                self.assertIsNone(reviewed["impact_frame"])

                def descendants(widget):
                    for child in widget.winfo_children():
                        yield child
                        yield from descendants(child)

                accept = next(widget for widget in descendants(window) if
                              widget.winfo_class() == "TButton" and str(widget.cget("text")).startswith("Accept reviewed"))
                self.assertTrue(accept.winfo_ismapped())
                self.assertLessEqual(accept.winfo_rootx() + accept.winfo_width(),
                                     window.winfo_rootx() + window.winfo_width())
                self.assertLessEqual(accept.winfo_rooty() + accept.winfo_height(),
                                     window.winfo_rooty() + window.winfo_height())
            finally:
                if app is not None:
                    app.close()
                else:
                    window.destroy()


if __name__ == "__main__":
    unittest.main()
