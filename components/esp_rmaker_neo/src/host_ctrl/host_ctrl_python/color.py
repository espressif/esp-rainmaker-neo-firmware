# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0


class bcolors:
    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"


class ColorFormatter:
    """
    Formats text with colors.
    """

    @staticmethod
    def format(text: str, color: str) -> str:
        return f"{color}{text}{bcolors.ENDC}"

    @staticmethod
    def header(text: str) -> str:
        return ColorFormatter.format(text, bcolors.HEADER)

    @staticmethod
    def error(text: str) -> str:
        return ColorFormatter.format(text, bcolors.FAIL)

    @staticmethod
    def warning(text: str) -> str:
        return ColorFormatter.format(text, bcolors.WARNING)

    @staticmethod
    def ok(text: str) -> str:
        return ColorFormatter.format(text, bcolors.OKGREEN)

    @staticmethod
    def ok_blue(text: str) -> str:
        return ColorFormatter.format(text, bcolors.OKBLUE)

    @staticmethod
    def ok_cyan(text: str) -> str:
        return ColorFormatter.format(text, bcolors.OKCYAN)
