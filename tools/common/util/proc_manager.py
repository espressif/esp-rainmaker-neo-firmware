# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import pty
import termios
import os
import subprocess
import sys


class ProcManager:
    @staticmethod
    def _open_pty() -> tuple[int, int]:
        """
        Open and configure a pseudo-terminal.
        """
        # Use a PTY so tools (like ninja) detect a TTY and keep carriage-return updates
        master_fd, slave_fd = pty.openpty()
        # Configure the slave PTY to avoid CR/NL translations and cooked mode
        try:
            attrs = termios.tcgetattr(slave_fd)
            icrnl = getattr(termios, "ICRNL", 0)
            inlcr = getattr(termios, "INLCR", 0)
            attrs[0] &= ~(icrnl | inlcr)
            oPOST = getattr(termios, "OPOST", 0)
            onlcr = getattr(termios, "ONLCR", 0)
            ocrnl = getattr(termios, "OCRNL", 0)
            attrs[1] &= ~(oPOST | onlcr | ocrnl)
            echo = getattr(termios, "ECHO", 0)
            icanon = getattr(termios, "ICANON", 0)
            attrs[3] &= ~(echo | icanon)
            termios.tcsetattr(slave_fd, termios.TCSANOW, attrs)
        except Exception:
            pass
        return master_fd, slave_fd

    @staticmethod
    def run_cmd_sync(cmd: list[str], env: dict = None) -> str:
        """
        Run a command synchronously, streaming stdout/stderr live to console.
        Returns the combined output as a string.
        """
        master_fd, slave_fd = ProcManager._open_pty()
        proc = subprocess.Popen(
            cmd,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            env=env,
        )
        os.close(slave_fd)

        # Read raw bytes from the PTY master and forward to real stdout
        master_file = os.fdopen(master_fd, "rb", buffering=0)
        output_chunks: list[bytes] = []
        try:

            def _write_bytes() -> bool:
                try:
                    data = master_file.read(1024)
                except OSError:
                    # PTY became invalid (race condition when subprocess exits)
                    return False
                if not data:
                    return False
                output_chunks.append(data)
                sys.stdout.write(data.decode("utf-8", errors="replace"))
                sys.stdout.flush()
                return True

            while proc.poll() is None:
                if not _write_bytes():
                    break

            # Drain any remaining data
            while True:
                if not _write_bytes():
                    break
        finally:
            try:
                master_file.close()
            except Exception:
                pass

        proc.wait()
        if proc.returncode != 0:
            combined = b"".join(output_chunks).decode("utf-8", errors="replace")
            raise RuntimeError(
                f"Command {' '.join(cmd)} failed with exit code {proc.returncode}:\n{combined}"
            )
        return b"".join(output_chunks).decode("utf-8", errors="replace")

    @staticmethod
    def run_cmd_interactive(
        cmd: list[str],
        env: dict = None,
        graceful_exit_sequence: bytes = b"",
        wait_timeout_seconds: float = 3.0,
    ) -> int:
        """
        Run a command interactively with bidirectional I/O:
        - Forwards user keyboard input (raw) to the child
        - Streams child output to this terminal
        - On KeyboardInterrupt, sends graceful_exit_sequence to child and waits briefly
        Returns the child exit code.
        """
        import tty
        import select

        master_fd, slave_fd = ProcManager._open_pty()
        proc = subprocess.Popen(
            cmd,
            stdin=slave_fd,
            stdout=slave_fd,
            stderr=slave_fd,
            close_fds=True,
            env=env,
        )
        os.close(slave_fd)

        stdin_fd = sys.stdin.fileno()
        old_stdin_attrs = termios.tcgetattr(stdin_fd)
        try:
            tty.setraw(stdin_fd)
        except Exception:
            pass

        try:
            while proc.poll() is None:
                rlist, _, _ = select.select([master_fd, stdin_fd], [], [], 0.05)
                if master_fd in rlist:
                    try:
                        data = os.read(master_fd, 4096)
                        if data:
                            sys.stdout.buffer.write(data)
                            sys.stdout.buffer.flush()
                    except OSError:
                        break
                if stdin_fd in rlist:
                    try:
                        data = os.read(stdin_fd, 4096)
                        if data:
                            os.write(master_fd, data)
                    except OSError:
                        pass
        except KeyboardInterrupt:
            try:
                if graceful_exit_sequence:
                    os.write(master_fd, graceful_exit_sequence)
                # Wait briefly for clean shutdown
                import time

                end_time = time.time() + wait_timeout_seconds
                while time.time() < end_time and proc.poll() is None:
                    try:
                        data = os.read(master_fd, 4096)
                        if data:
                            sys.stdout.buffer.write(data)
                            sys.stdout.buffer.flush()
                    except OSError:
                        break
                    time.sleep(0.05)
                if proc.poll() is None:
                    proc.terminate()
            except Exception:
                try:
                    proc.terminate()
                except Exception:
                    pass
        finally:
            try:
                termios.tcsetattr(stdin_fd, termios.TCSANOW, old_stdin_attrs)
            except Exception:
                pass
            try:
                os.close(master_fd)
            except Exception:
                pass

        try:
            proc.wait(timeout=1)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass
            proc.wait()

        return int(proc.returncode or 0)
