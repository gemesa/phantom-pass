SUBDIRS := \
src/0-hello-world \
src/1-string-xor-encryption \
src/2-string-base64-encoding \
src/3-string-xor-encryption \
src/4-string-rc4-encryption \
src/5-mba-add \
src/6-mba-sub \
src/7-mba-const \
src/8-ptrace-deny \
src/9-ptrace-deny-asm \
src/10-frida-deny-basic \
src/11-frida-deny-complex \
src/12-frida-deny-with-runtime-check

.PHONY: all clean run format format-check $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(SUBDIRS:%=clean-%)

$(SUBDIRS:%=clean-%):
	-$(MAKE) -C $(@:clean-%=%) clean

run: $(SUBDIRS:%=run-%)

$(SUBDIRS:%=run-%):
	$(MAKE) -C $(@:run-%=%) run

format: $(SUBDIRS:%=format-%)

$(SUBDIRS:%=format-%):
	$(MAKE) -C $(@:format-%=%) format

format-check: $(SUBDIRS:%=format-check-%)

$(SUBDIRS:%=format-check-%):
	$(MAKE) -C $(@:format-check-%=%) format-check
