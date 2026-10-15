#ifndef NAMEFILTER_H
#define NAMEFILTER_H

#include <QRegularExpression>
#include <QString>

// The matching rule behind the file list's name filter (Ctrl + F): pure, so
// it can be unit tested without a model or a widget.
//
// Two modes, chosen by the pattern itself:
//   - no `*` or `?` in it  → case-insensitive substring ("2024" finds
//     "IMG_2024_001.jpg" and "Trip 2024");
//   - any wildcard         → the whole name must match, Explorer / Total
//     Commander style ("*.jpg" is every JPEG, "IMG_2?.*" a numbered run).
// A pattern that is empty or only whitespace matches everything, which is
// what "the filter is cleared" means to the proxy.
//
// Build a NameFilter once per pattern change and ask it per row; compiling
// the wildcard expression per row would be the slow part of filtering a
// 10k-file folder.
class NameFilter
{
public:
    NameFilter() = default;
    explicit NameFilter(const QString& pattern);

    // True when the pattern matches everything (empty / whitespace only).
    bool isEmpty() const { return _empty; }
    QString pattern() const { return _pattern; }

    bool matches(const QString& name) const;

    // Convenience for one-off use and the tests.
    static bool matches(const QString& name, const QString& pattern);
    static bool hasWildcards(const QString& pattern);

private:
    QString _pattern;
    bool _empty = true;
    bool _wild = false;
    QString _needle;             // substring mode: the trimmed pattern
    QRegularExpression _regex;   // wildcard mode
};

#endif // NAMEFILTER_H
