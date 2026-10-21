# Keep the build rules in src; GNU make is required for both targets.
.DEFAULT_GOAL := opengl
.PHONY: opengl matrox clean clean-opengl clean-matrox
opengl matrox clean clean-opengl clean-matrox:
	$(MAKE) -C src $@
