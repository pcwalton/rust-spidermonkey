OSNAME=$(shell uname -s)
ifeq ($(OSNAME),Linux)
  DYLIB=so
  LDFLAGS_DYNAMICLIB=-shared
else
  DYLIB=dylib
  LDFLAGS_DYNAMICLIB=-dynamiclib
endif

LIB=lib
RUSTC?=rustc
CXX=g++
CXXFLAGS+=-g
LIBS=-lmozjs -lrustrt
VERSION=0.1

LDFLAGS+=$(LDFLAGS_DYNAMICLIB)

all:    $(LIB)spidermonkey-$(VERSION).$(DYLIB)

$(LIB)spidermonkey-$(VERSION).$(DYLIB):	spidermonkey.rc js.rs $(LIB)spidermonkeyrustext.$(DYLIB)
	$(RUSTC) -o $@ --lib $<

$(LIB)spidermonkeyrustext.$(DYLIB):	spidermonkeyrustext.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(LIBS) -o $@ $<

test:	test.rs $(LIB)spidermonkey-$(VERSION).$(DYLIB)
	$(RUSTC) -o $@ -L . $<

.PHONY:	clean

clean:
	rm -f $(LIB)spidermonkey-$(VERSION).$(DYLIB) $(LIB)spidermonkeyrustext.$(DYLIB) test

