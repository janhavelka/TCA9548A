#!/usr/bin/env python3
"""Host-only tests for HIL evidence persistence and failure semantics."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import tca9548a_hil as hil


HOST = {"branch": "main", "commit": "a" * 40, "worktree": "clean", "status_short": ""}


class ArtifactTests(unittest.TestCase):
    def run_main(self, argv):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()), \
                patch.object(hil, "repository_provenance", return_value=HOST.copy()), \
                patch.object(hil, "tool_text", return_value="test tool"):
            return hil.main(argv)

    def test_dry_bundle_retains_plan_and_hashes_without_claiming_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            argv = ["--dry-run", "--output-dir", directory, "--fixture-note", "Fixture with spaces"]
            with patch.object(hil, "SerialRunner") as serial, patch.object(hil.sys, "argv", ["wrong invocation"]):
                self.assertEqual(self.run_main(argv), 0)
            serial.assert_not_called()
            run_dir, = Path(directory).iterdir()
            self.assertEqual({path.name for path in run_dir.iterdir()}, {"report.md", "results.json", "manifest.json"})
            results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(results["schema_version"], 1)
            self.assertEqual(results["overall_status"], hil.NOT_RUN)
            self.assertEqual(results["exit_code"], 0)
            self.assertEqual(results["run"]["argv"], argv)
            self.assertEqual(results["run"]["host_start"], HOST)
            self.assertFalse(results["run"]["host_changed"])
            self.assertLessEqual(results["run"]["started_utc"], results["run"]["finished_utc"])
            self.assertTrue(all(result["status"] == hil.NOT_RUN for result in results["results"]))
            self.assertIsNone(results["firmware_version_response"])
            report = (run_dir / "report.md").read_text(encoding="utf-8")
            self.assertIn("Fixture with spaces", report)
            self.assertNotIn("wrong invocation", report)
            self.assertIn("serial port was not opened", report)
            manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["run_id"], run_dir.name)
            self.assertEqual(manifest["overall_status"], hil.NOT_RUN)
            self.assertEqual(manifest["runner"]["sha256"], hil.sha256_file(Path(hil.__file__)))
            for artifact in manifest["artifacts"].values():
                path = Path(artifact["path"])
                self.assertEqual(artifact["sha256"], hil.sha256_file(path))
                self.assertEqual(artifact["bytes"], path.stat().st_size)

    def test_repeated_runs_create_distinct_directories_and_keep_previous_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            argv = ["--dry-run", "--output-dir", directory]
            self.assertEqual(self.run_main(argv), 0)
            first, = Path(directory).iterdir()
            original = (first / "results.json").read_bytes()
            self.assertEqual(self.run_main(argv), 0)
            self.assertEqual(len(list(Path(directory).iterdir())), 2)
            self.assertEqual((first / "results.json").read_bytes(), original)

    def test_existing_legacy_artifacts_are_preserved_before_opening_serial(self):
        for flag in ("--report", "--transcript"):
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "prior.txt"
                path.write_text("prior evidence", encoding="utf-8")
                with patch.object(hil, "SerialRunner") as serial:
                    self.assertEqual(self.run_main([flag, str(path), "--allow-not-run"]), 1)
                serial.assert_not_called()
                self.assertEqual(path.read_text(encoding="utf-8"), "prior evidence")

    def test_invalid_output_parent_prevents_hardware_access(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "file"
            parent.write_text("original", encoding="utf-8")
            with patch.object(hil, "SerialRunner") as serial:
                self.assertEqual(self.run_main(["--report", str(parent / "report.md")]), 1)
            serial.assert_not_called()

    def test_output_aliases_and_mixed_output_modes_are_rejected(self):
        for argv in (["--report", "same.txt", "--transcript", "child/../same.txt"],
                     ["--output-dir", "runs", "--report", "report.md"],
                     ["--output-dir", "runs", "--transcript", "transcript.log"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    hil.parse_args(argv)

    def test_complete_firmware_identity_and_live_transcript_are_preserved(self):
        version = "=== Version Info ===\nLibrary: TCA9548A\n" + "framework detail\n" * 20 + "Firmware commit: " + "b" * 40

        def live(args):
            args._firmware_version = version
            transcript = args._artifact_handles[args.transcript.resolve()]
            transcript.write("$ version\n" + version + "\n")
            transcript.close()
            return [hil.Result(hil.build_plan(args)[0], hil.PASS, "short excerpt", 0.25)], args.transcript

        with tempfile.TemporaryDirectory() as directory, patch.object(hil, "run_live", side_effect=live):
            self.assertEqual(self.run_main(["--output-dir", directory]), 0)
            run_dir, = Path(directory).iterdir()
            results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(results["firmware_version_response"], version)
            self.assertEqual(results["overall_status"], hil.PASS)
            self.assertIn(version, (run_dir / "report.md").read_text(encoding="utf-8"))
            manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["artifacts"]["transcript"]["sha256"], hil.sha256_file(run_dir / "transcript.log"))

    def test_report_failure_is_nonzero_and_recorded_in_json(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(hil, "write_report", side_effect=OSError("disk full")):
            self.assertEqual(self.run_main(["--dry-run", "--output-dir", directory, "--allow-not-run"]), 1)
            run_dir, = Path(directory).iterdir()
            results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(results["overall_status"], hil.FAIL)
            self.assertEqual(results["exit_code"], 1)
            self.assertIn("disk full", results["artifact_errors"][0])

    def test_bundle_write_failure_returns_nonzero_without_traceback(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(hil, "write_bundle", side_effect=OSError("unavailable disk")):
            self.assertEqual(self.run_main(["--dry-run", "--output-dir", directory]), 1)

    def test_close_failures_cannot_seal_a_pass_bundle(self):
        original_open = Path.open

        class FailingClose:
            def __init__(self, handle):
                self.handle = handle

            def __getattr__(self, name):
                return getattr(self.handle, name)

            def close(self):
                self.handle.close()
                raise OSError("close failed after buffered bytes were written")

        for name in ("report.md", "results.json", "manifest.json"):
            def open_file(path, mode="r", *args, **kwargs):
                handle = original_open(path, mode, *args, **kwargs)
                return FailingClose(handle) if path.name == name and mode == "x" else handle

            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                with patch.object(Path, "open", new=open_file):
                    self.assertEqual(self.run_main(["--dry-run", "--output-dir", directory]), 1)
                run_dir, = Path(directory).iterdir()
                if name == "report.md":
                    results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
                    manifest = json.loads((run_dir / "manifest.json").read_text(encoding="utf-8"))
                    self.assertEqual(results["exit_code"], 1)
                    self.assertEqual(manifest["overall_status"], hil.FAIL)
                else:
                    self.assertFalse((run_dir / "manifest.json").exists())

    def test_host_and_runner_changes_are_recorded(self):
        changed = dict(HOST, commit="b" * 40)
        hash_file = hil.sha256_file
        calls = 0

        def changing_hash(path):
            nonlocal calls
            if path == Path(hil.__file__).resolve():
                calls += 1
                return ("a" if calls == 1 else "b") * 64
            return hash_file(path)

        with tempfile.TemporaryDirectory() as directory, \
                patch.object(hil, "repository_provenance", side_effect=[HOST, changed]), \
                patch.object(hil, "sha256_file", side_effect=changing_hash), \
                patch.object(hil, "tool_text", return_value="test tool"), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(hil.main(["--dry-run", "--output-dir", directory]), 0)
            run_dir, = Path(directory).iterdir()
            results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
            self.assertTrue(results["run"]["host_changed"])
            self.assertTrue(results["run"]["runner_changed"])
            self.assertEqual(results["run"]["host_end"], changed)

    def test_transcript_failure_is_not_hidden_by_allow_not_run(self):
        def live(args):
            hil.artifact_error(args, "transcript flush failed")
            return hil.dry_run(args), None

        with patch.object(hil, "run_live", side_effect=live):
            self.assertEqual(self.run_main(["--allow-not-run"]), 1)

    def test_serial_unavailable_is_not_hidden_or_reported_as_executed(self):
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(hil, "SerialRunner", side_effect=RuntimeError("port unavailable")):
            self.assertEqual(self.run_main(["--output-dir", directory, "--allow-not-run"]), 1)
            run_dir, = Path(directory).iterdir()
            results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(results["overall_status"], hil.UNKNOWN)
            self.assertEqual(results["results"][0]["status"], hil.UNKNOWN)
            report = (run_dir / "report.md").read_text(encoding="utf-8")
            self.assertIn("no live step executed", report)
            self.assertNotIn("Mode: live serial commands executed", report)

    def test_overall_status_never_turns_omitted_tests_into_a_pass(self):
        result = hil.Result(hil.build_plan(hil.parse_args([]))[0], hil.NOT_RUN, "omitted")
        self.assertEqual(hil.result_exit_code([result], dry_run=False, allow_not_run=True), 0)
        self.assertEqual(hil.overall_status([result]), hil.NOT_RUN)
        self.assertEqual(hil.overall_status([]), hil.NOT_RUN)
        self.assertEqual(hil.overall_status([result], ["artifact lost"]), hil.FAIL)


if __name__ == "__main__":
    unittest.main()
