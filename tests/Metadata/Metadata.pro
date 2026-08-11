QT       += core gui testlib
CONFIG   += c++17 console testcase
CONFIG   -= app_bundle

TEMPLATE = app
TARGET   = tst_Metadata

INCLUDEPATH += $$PWD/.. $$PWD/../../src

SOURCES += \
    tst_Metadata.cpp \
    $$PWD/../../src/MetadataReader.cpp

HEADERS += \
    $$PWD/../../src/MetadataReader.h \
    $$PWD/../../src/ImageMetadata.h

# Optional Exiv2 backend — mirror Pixee.pro. With PIXEE_HAVE_EXIV2=1 the reader
# parses EXIF/IPTC/XMP and the round-trip test exercises it; without it the
# suite tests the Qt-only basics (and skips the EXIF round-trip).
!isEmpty(PIXEE_HAVE_EXIV2) {
    DEFINES     += PIXEE_HAVE_EXIV2
    INCLUDEPATH += $$PWD/../../thirdparty/exiv2/include
    LIBS        += -L$$PWD/../../thirdparty/exiv2/lib -lexiv2
}
