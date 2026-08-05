# Local convenience wrapper around esp-docs' build-docs.
#
# sphinx-build is NOT called directly: the Doxygen run and the per-header .inc
# fragments are driven by config values (project_path, doxyfile_dir, build_dir)
# that only build-docs passes in.
#
# Requires: pip install -r requirements.txt, plus doxygen on PATH.

LANGUAGE   ?= en
BUILDDIR   ?= _build
# Repo root, one level up from docs/.
PROJECTDIR ?= ..
BUILD_DOCS ?= build-docs

# Docs are target-agnostic -- no -t, so output lands in $(BUILDDIR)/$(LANGUAGE)/generic/.
BUILD_DOCS_ARGS = -l $(LANGUAGE) -pp $(PROJECTDIR) -b $(BUILDDIR)

.PHONY: help html fast linkcheck clean

help:
	@echo 'make html       - build the HTML docs into $(BUILDDIR)/$(LANGUAGE)/generic/html'
	@echo 'make fast       - same, skipping the Doxygen API includes (quick rst iteration)'
	@echo 'make linkcheck  - check external links'
	@echo 'make clean      - remove $(BUILDDIR)'

html:
	$(BUILD_DOCS) $(BUILD_DOCS_ARGS)

fast:
	$(BUILD_DOCS) $(BUILD_DOCS_ARGS) --fast-build

linkcheck:
	$(BUILD_DOCS) $(BUILD_DOCS_ARGS) linkcheck

clean:
	rm -rf $(BUILDDIR)
