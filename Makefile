# wrapper for meson

all: compile

BUILDDIR = builddir

$(BUILDDIR):
	meson setup $(BUILDDIR)

compile: $(BUILDDIR)
	meson compile -C $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all compile clean
