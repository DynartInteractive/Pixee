#include "NameFilter.h"

NameFilter::NameFilter(const QString& pattern)
    : _pattern(pattern)
{
    const QString trimmed = pattern.trimmed();
    _empty = trimmed.isEmpty();
    if (_empty) return;
    _wild = hasWildcards(trimmed);
    if (_wild) {
        // Anchored (the default conversion): the whole name has to fit the
        // pattern. A file name never contains a path separator, so the
        // conversion's path-aware `*` behaves like a plain one here.
        _regex = QRegularExpression(
            QRegularExpression::wildcardToRegularExpression(trimmed),
            QRegularExpression::CaseInsensitiveOption);
    } else {
        _needle = trimmed;
    }
}

bool NameFilter::hasWildcards(const QString& pattern) {
    return pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'));
}

bool NameFilter::matches(const QString& name) const {
    if (_empty) return true;
    if (_wild) return _regex.match(name).hasMatch();
    return name.contains(_needle, Qt::CaseInsensitive);
}

bool NameFilter::matches(const QString& name, const QString& pattern) {
    return NameFilter(pattern).matches(name);
}
