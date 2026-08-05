# Contributing

Contributions are welcome as pull requests on GitHub. The
[ESP-IDF Contributions Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/index.html)
applies to this project as well, in particular for code style and the
contributor agreement. Before submitting, install the pre-commit hooks below
(CI runs the same checks) and fill in the pull request template.

## Pre-Commit Hooks

Code maintenance is achieved with [pre-commit](https://pre-commit.com) hooks [here](.pre-commit-config.yaml):

- Using [astyle](https://astyle.sourceforge.net) for code formatting
- Using [cmake-format](https://github.com/cheshirekow/cmake-format-precommit) for CMake formatting
- Using [ruff](https://github.com/astral-sh/ruff-pre-commit) for Python linting + formatting
- Using [esp-idf-kconfig](https://github.com/espressif/esp-idf-kconfig.git) for Kconfig checking
- Using [codespell](https://github.com/codespell-project/codespell) for common misspellings
- Using [insert-license](https://github.com/Lucas-C/pre-commit-hooks?tab=readme-ov-file#insert-license) for license headers
- Using [end-of-file-fixer](https://github.com/pre-commit/pre-commit-hooks?tab=readme-ov-file#end-of-file-fixer) to ensure end-of-file newline compliance

### Setup

In your Python environment, run

```bash
pip install pre-commit
```

### Usage

To install the hooks to run before every commit is finalized:

```bash
pre-commit install
```

To run on all files (not just staged), at any point of time:

```bash
pre-commit run --all-files
```

## Testing Changes

Use the unit-test applications for [ESP-IDF](test/apps/esp-idf/) or
[POSIX](test/apps/posix/); PyTest integration tests are described in
[test/README.md](test/README.md).
