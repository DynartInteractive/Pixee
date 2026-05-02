TEMPLATE = subdirs

# One subdirectory per test binary — a crash in one doesn't take the
# others down. Add a new entry for each tst_*.cpp suite.
SUBDIRS = \
    FileOpsHelpers \
    CopyFileTask
