"""Read PLUG_MFR_ID and PLUG_UNIQUE_ID from config.h with comments stripped.

iPlug2 Scripts/parse_config.py extractStringElement copies the rest of the
#define line and strips quotes only from the ends. An inline // comment
therefore becomes part of the Audio Unit manufacturer code. The C++ compiler
drops that comment, so the build stays green while the host drops the plugin.

A FourCC here is the four characters inside one active #define literal.
Comments are outside the token. install_fourccs overwrites the two strings
parse_config took from the raw line before a resource script writes a plist.
"""

import os
import plistlib
import re

MANUFACTURER = "PLUG_MFR_ID"
SUBTYPE = "PLUG_UNIQUE_ID"
FIELDS = (MANUFACTURER, SUBTYPE)
SAMPLE_IDS = frozenset(("Acme", "PmBl", "Ipef"))

_DEFINE = re.compile(
    r"^[ \t]*#[ \t]*define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*(.*)$"
)
_COMMENTED_DEFINE = re.compile(
    r"#[ \t]*define[ \t]+(PLUG_MFR_ID|PLUG_UNIQUE_ID)\b(.*)"
)
_QUOTED = re.compile(r"['\"]([^'\"]*)['\"]")


class PlugIdentityError(Exception):
    """The manufacturer or subtype FourCC cannot be registered as-is."""

    def __init__(self, field, replacement, message):
        super().__init__(message)
        self.field = field
        self.replacement = replacement


def _raise(field, replacement, reason):
    if replacement is None:
        message = f"{field} {reason}; replace it"
    else:
        message = f"{field} {reason}; replace '{replacement}'"
    raise PlugIdentityError(field, replacement, message)


def _first_literal(text):
    stripped = text.strip()
    if not stripped or stripped[0] not in ("'", '"'):
        return None
    quote = stripped[0]
    chars = []
    index = 1
    while index < len(stripped):
        if stripped[index] == "\\":
            if index + 1 < len(stripped):
                chars.append(stripped[index + 1])
                index += 2
                continue
            break
        if stripped[index] == quote:
            return "".join(chars)
        chars.append(stripped[index])
        index += 1
    return "".join(chars)


def _quoted_anywhere(text):
    match = _QUOTED.search(text)
    if match is None:
        return None
    return match.group(1)


def _split_line(line):
    """Split one line into code and comment text. Quotes hide comment markers."""
    code = []
    comment = []
    index = 0
    limit = len(line)
    while index < limit:
        if line.startswith("//", index):
            comment.append(line[index:])
            break
        if line.startswith("/*", index):
            end = line.find("*/", index + 2)
            if end == -1:
                comment.append(line[index:])
                return "".join(code), "".join(comment), True
            comment.append(line[index:end + 2])
            index = end + 2
            continue
        character = line[index]
        if character in ("'", '"'):
            cursor = index + 1
            while cursor < limit:
                if line[cursor] == "\\":
                    cursor += 2
                    continue
                if line[cursor] == character:
                    cursor += 1
                    break
                cursor += 1
            code.append(line[index:cursor])
            index = cursor
            continue
        code.append(character)
        index += 1
    return "".join(code), "".join(comment), False


def _note_comment(comment, commented):
    for match in _COMMENTED_DEFINE.finditer(comment):
        commented[match.group(1)] = _quoted_anywhere(match.group(2))


def _scan(text):
    """Return (active, commented).

    active maps a field to ('literal', token), ('comment', token or None),
    or ('empty', None). The last active #define wins, matching the preprocessor.
    commented maps a field seen only inside a comment to the quoted token, if any.
    """
    active = {}
    commented = {}
    in_block = False
    for line in text.splitlines():
        if in_block:
            end = line.find("*/")
            if end == -1:
                _note_comment(line, commented)
                continue
            _note_comment(line[:end], commented)
            line = line[end + 2:]
            in_block = False
        code, comment, in_block = _split_line(line)
        _note_comment(comment, commented)
        match = _DEFINE.match(code)
        if match is None:
            continue
        name = match.group(1)
        if name not in FIELDS:
            continue
        rest = match.group(2)
        if rest.strip() == "":
            if comment.strip():
                active[name] = ("comment", _quoted_anywhere(comment))
            else:
                active[name] = ("empty", None)
            continue
        literal = _first_literal(rest)
        if literal is None:
            bare = rest.strip().split()[0]
            active[name] = ("bare", bare)
        else:
            active[name] = ("literal", literal)
    return active, commented


def _validate(field, token):
    printable = len(token) == 4 and all(32 <= ord(character) <= 126 for character in token)
    if not printable:
        _raise(field, token, "is not 4 printable characters")
    if field == MANUFACTURER and token == token.lower() and token != token.upper():
        _raise(field, token, "is all lowercase")
    if token in SAMPLE_IDS:
        _raise(field, token, "is a sample id")
    return token


def fourcc(text, field):
    """Return one active FourCC from config.h text, or raise PlugIdentityError."""
    if field not in FIELDS:
        _raise(field, None, "is not an Audio Unit FourCC field")
    active, commented = _scan(text)
    if field not in active:
        if field in commented:
            _raise(field, commented[field], "is commented out")
        _raise(field, None, "is missing")
    kind, token = active[field]
    if kind == "comment":
        _raise(field, token, "is only a comment")
    if kind == "empty":
        _raise(field, None, "is missing a FourCC token")
    if kind == "bare":
        _raise(field, token, "is not a quoted 4-character literal")
    return _validate(field, token)


def read_fourccs(text):
    """Return PLUG_MFR_ID and PLUG_UNIQUE_ID parsed from config.h text."""
    return {field: fourcc(text, field) for field in FIELDS}


def read_config(path):
    with open(path, "r", encoding="utf-8") as handle:
        return read_fourccs(handle.read())


def install_fourccs(config, projectpath):
    """Overwrite the FourCCs parse_config copied from the raw #define line.

    Raises PlugIdentityError when a code is missing, commented out, not 4
    printable characters, an all-lowercase manufacturer id, or still a sample
    id. Resource scripts exit 1 on that error before they write a plist.
    """
    found = read_config(os.path.join(projectpath, "config.h"))
    for field in FIELDS:
        config[field] = found[field]
    return config


def _component_pairs(node, found):
    if isinstance(node, dict):
        components = node.get("AudioComponents")
        if isinstance(components, list):
            for item in components:
                if isinstance(item, dict):
                    manufacturer = item.get("manufacturer", "")
                    subtype = item.get("subtype", "")
                    if not isinstance(manufacturer, str):
                        manufacturer = ""
                    if not isinstance(subtype, str):
                        subtype = ""
                    found.append((manufacturer, subtype))
        for value in node.values():
            _component_pairs(value, found)
    elif isinstance(node, list):
        for item in node:
            _component_pairs(item, found)


def component_pairs(path):
    with open(path, "rb") as handle:
        data = plistlib.load(handle)
    found = []
    _component_pairs(data, found)
    return found


def registration_mismatch(rel_path, manufacturer, subtype, expected_mfr, expected_sub):
    return (
        f"{rel_path} registers manufacturer '{manufacturer}' subtype '{subtype}'. "
        f"config.h says '{expected_mfr}' / '{expected_sub}'."
    )


def check_resources(root):
    """Compare every committed AudioComponents plist under root to config.h.

    Returns the success line. Raises PlugIdentityError naming the plist that
    disagrees. Does not build an Audio Unit.
    """
    identity = read_config(os.path.join(root, "config.h"))
    matched = []
    resources = os.path.join(root, "resources")
    for name in sorted(os.listdir(resources)):
        if not name.endswith(".plist"):
            continue
        path = os.path.join(resources, name)
        pairs = component_pairs(path)
        if not pairs:
            continue
        rel_path = "resources/" + name
        for manufacturer, subtype in pairs:
            if manufacturer != identity[MANUFACTURER] or subtype != identity[SUBTYPE]:
                raise PlugIdentityError(
                    MANUFACTURER,
                    manufacturer,
                    registration_mismatch(
                        rel_path,
                        manufacturer,
                        subtype,
                        identity[MANUFACTURER],
                        identity[SUBTYPE],
                    ),
                )
        matched.append(rel_path)
    if not matched:
        _raise(MANUFACTURER, None, "has no AudioComponents plist under resources/")
    listed = ", ".join(matched)
    return (
        f"Audio Unit identity {identity[MANUFACTURER]} / {identity[SUBTYPE]} "
        f"matches {listed}. This did not build the Audio Unit."
    )
