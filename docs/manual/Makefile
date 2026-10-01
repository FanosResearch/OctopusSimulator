# make            build the manual (figures, generated tables, latexmk)
# make figures    re-render every SVG figure
# make clean      remove the latexmk aux directory
.PHONY: all figures clean
all:
	bash build.sh
figures:
	bash build.sh --force-figures
clean:
	bash build.sh --clean
