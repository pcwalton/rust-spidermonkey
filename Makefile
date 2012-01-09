LIB=lib
DYLIB=dylib
RUSTC?=rustc
CXX=g++
LDFLAGS_DYNAMICLIB=-dynamiclib
LIBS=-lmozjs -lrustrt
VERSION=0.1

LDFLAGS+=$(LDFLAGS_DYNAMICLIB)

all:    $(LIB)spidermonkey-$(VERSION).$(DYLIB)

$(LIB)spidermonkey-$(VERSION).$(DYLIB):	spidermonkey.rc js.rs $(LIB)spidermonkeyrustext.$(DYLIB)
	$(RUSTC) -o $@ --lib $<

$(LIB)spidermonkeyrustext.$(DYLIB):	spidermonkeyrustext.cpp
	$(CXX) -o $@ $(LDFLAGS_DYNAMICLIB) $(LDFLAGS) $(LIBS) -o $@ $<

test:	test.rs $(LIB)spidermonkey-$(VERSION).$(DYLIB)
	$(RUSTC) -o $@ -L . $<

.PHONY:	clean

clean:
	rm -f $(LIB)spidermonkey-$(VERSION).$(DYLIB) $(LIB)spidermonkeyrustext.$(DYLIB)

