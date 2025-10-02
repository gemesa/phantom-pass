SUBDIRS := \
src/0-hello-world \
src/1-string-xor-encryption \
src/2-string-base64-encoding \
src/3-string-xor-encryption

.PHONY: all clean run $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

run:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir run; \
	done