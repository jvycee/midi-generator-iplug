#!/usr/bin/env python3
"""Audio Unit identity checks for config.h and the committed plists.

The historical extractor below is a copy of iPlug2
Scripts/parse_config.py extractStringElement. It keeps an inline comment.
The reader in scripts/plug_identity.py must not.

Running this file checks the committed tree. It does not build an Audio Unit.
"""

import os
import plistlib
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

from plug_identity import (  # noqa: E402
    PlugIdentityError,
    check_resources,
    fourcc,
    install_fourccs,
    read_fourccs,
)

# config.h at 6cffbc277^ (dc7e62e5f's line, still present on the parent).
# 161 characters once the trailing newline is dropped and the opening quote
# is stripped from the end only: the closing quote is not at the end.
HISTORICAL_LINE = (
    "#define PLUG_MFR_ID 'Jvce'  // 4-char manufacturer code (AU/VST FourCC); "
    "derived from Jvycee. Must stay exactly 4 chars with at least one uppercase "
    "(all-lowercase is Apple-reserved).\n"
)


def extractStringElement(line, macro, config):
    """Copy of iPlug2 extractStringElement. Quotes are stripped from the ends."""
    lineText = "#define " + macro + " "
    if lineText in line:
        if '"' in line:
            config[macro] = line[len(lineText):-1].strip('"')
        elif "'" in line:
            config[macro] = line[len(lineText):-1].strip("'")
        else:
            config[macro] = line[len(lineText):-1]
        return True
    return False


def fail(message):
    raise SystemExit("FAIL: " + message)


def expect_reject(text, field, snippet):
    try:
        fourcc(text, field)
    except PlugIdentityError as error:
        message = str(error)
        if error.field != field or field not in message:
            fail(f"{field} error did not name the field: {message}")
        if snippet is not None and snippet not in message:
            fail(f"{field} error did not name the replacement {snippet!r}: {message}")
        return
    fail(f"{field} accepted {text!r}")


def test_historical_line_stays_corrupt_for_the_old_extractor():
    config = {}
    if not extractStringElement(HISTORICAL_LINE, "PLUG_MFR_ID", config):
        fail("historical line was not recognized by extractStringElement")
    raw = config["PLUG_MFR_ID"]
    if len(raw) <= 4:
        fail(f"historical extract was {len(raw)} characters, expected longer than 4")
    if not raw.startswith("Jvce'  // 4-char"):
        fail(f"historical extract did not keep the comment: {raw!r}")
    if len(raw) != 161:
        fail(f"historical extract was {len(raw)} characters, expected 161")
    if fourcc(HISTORICAL_LINE, "PLUG_MFR_ID") != "Jvce":
        fail("comment-stripping reader did not return Jvce for the historical line")


def test_inline_comment_on_both_fourccs():
    text = (
        "#define PLUG_UNIQUE_ID 'dm39' // subtype comment\n"
        "#define PLUG_MFR_ID 'Jvce' // manufacturer comment\n"
    )
    found = read_fourccs(text)
    if found != {"PLUG_MFR_ID": "Jvce", "PLUG_UNIQUE_ID": "dm39"}:
        fail(f"inline comments leaked into the FourCCs: {found}")


def test_rejects_commented_out_define():
    text = "// #define PLUG_MFR_ID 'Jvce'\n#define PLUG_UNIQUE_ID 'dm39'\n"
    expect_reject(text, "PLUG_MFR_ID", "Jvce")
    block = "/*\n#define PLUG_MFR_ID 'Acme'\n*/\n#define PLUG_UNIQUE_ID 'dm39'\n"
    expect_reject(block, "PLUG_MFR_ID", "Acme")


def test_rejects_token_that_is_only_a_comment():
    expect_reject(
        "#define PLUG_MFR_ID // forgotten\n#define PLUG_UNIQUE_ID 'dm39'\n",
        "PLUG_MFR_ID",
        None,
    )
    expect_reject(
        "#define PLUG_MFR_ID /* 'Acme' */\n#define PLUG_UNIQUE_ID 'dm39'\n",
        "PLUG_MFR_ID",
        "Acme",
    )


def test_rejects_lowercase_and_sample_ids():
    expect_reject(
        "#define PLUG_MFR_ID 'jvce'\n#define PLUG_UNIQUE_ID 'dm39'\n",
        "PLUG_MFR_ID",
        "jvce",
    )
    expect_reject(
        "#define PLUG_MFR_ID 'Acme'\n#define PLUG_UNIQUE_ID 'dm39'\n",
        "PLUG_MFR_ID",
        "Acme",
    )
    expect_reject(
        "#define PLUG_MFR_ID 'Jvce'\n#define PLUG_UNIQUE_ID 'PmBl'\n",
        "PLUG_UNIQUE_ID",
        "PmBl",
    )
    expect_reject(
        "#define PLUG_MFR_ID 'Jvce'\n#define PLUG_UNIQUE_ID 'Ipef'\n",
        "PLUG_UNIQUE_ID",
        "Ipef",
    )
    # Subtype dm39 is all lowercase and is the real plugin id. Only the
    # manufacturer code is Apple-reserved when it has no uppercase letter.
    found = read_fourccs("#define PLUG_MFR_ID 'Jvce'\n#define PLUG_UNIQUE_ID 'dm39'\n")
    if found["PLUG_UNIQUE_ID"] != "dm39":
        fail("all-lowercase subtype dm39 was rejected")


def test_rejects_missing_define():
    expect_reject("#define PLUG_UNIQUE_ID 'dm39'\n", "PLUG_MFR_ID", None)
    expect_reject("#define PLUG_MFR_ID 'Jvce'\n", "PLUG_UNIQUE_ID", None)


def test_install_fourccs_overwrites_the_raw_line():
    config = {
        "PLUG_MFR_ID": "Jvce'  // 4-char manufacturer code",
        "PLUG_UNIQUE_ID": "dm39' // leftover",
    }
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "config.h"), "w", encoding="utf-8") as handle:
            handle.write(HISTORICAL_LINE)
            handle.write("#define PLUG_UNIQUE_ID 'dm39' // subtype\n")
        install_fourccs(config, tmp)
    if config["PLUG_MFR_ID"] != "Jvce" or config["PLUG_UNIQUE_ID"] != "dm39":
        fail(f"install_fourccs left the raw line in place: {config}")


def test_install_fourccs_rejects_sample_id():
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tmp, "config.h"), "w", encoding="utf-8") as handle:
            handle.write("#define PLUG_MFR_ID 'Acme'\n#define PLUG_UNIQUE_ID 'dm39'\n")
        try:
            install_fourccs({}, tmp)
        except PlugIdentityError as error:
            if error.field != "PLUG_MFR_ID" or "Acme" not in str(error):
                fail(f"sample id error did not name the field and replacement: {error}")
            return
    fail("install_fourccs accepted Acme")


def test_rejects_acme_pmbl_plist_pair():
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "resources"))
        with open(os.path.join(tmp, "config.h"), "w", encoding="utf-8") as handle:
            handle.write("#define PLUG_MFR_ID 'Jvce'\n#define PLUG_UNIQUE_ID 'dm39'\n")
        plist_path = os.path.join(tmp, "resources", "Drift-iOS-AUv3-Info.plist")
        with open(plist_path, "wb") as handle:
            plistlib.dump(
                {
                    "NSExtension": {
                        "NSExtensionAttributes": {
                            "AudioComponents": [
                                {"manufacturer": "Acme", "subtype": "PmBl"}
                            ]
                        }
                    }
                },
                handle,
            )
        try:
            check_resources(tmp)
        except PlugIdentityError as error:
            expected = (
                "resources/Drift-iOS-AUv3-Info.plist registers manufacturer 'Acme' "
                "subtype 'PmBl'. config.h says 'Jvce' / 'dm39'."
            )
            if str(error) != expected:
                fail(f"plist mismatch message was {str(error)!r}")
            return
    fail("Acme / PmBl plist pair was accepted")


def test_resource_scripts_exit_before_writing_a_plist():
    for name in ("prepare_resources-mac.py", "prepare_resources-ios.py"):
        path = os.path.join(ROOT, "scripts", name)
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
        install_at = text.find("install_fourccs(")
        dump_at = text.find("plistlib.dump")
        exit_at = text.find("sys.exit(1)")
        if install_at < 0 or dump_at < 0 or install_at > dump_at:
            fail(f"{name} does not overwrite FourCCs before writing a plist")
        if exit_at < 0 or "PlugIdentityError" not in text:
            fail(f"{name} does not exit 1 on PlugIdentityError")


def main():
    test_historical_line_stays_corrupt_for_the_old_extractor()
    test_inline_comment_on_both_fourccs()
    test_rejects_commented_out_define()
    test_rejects_token_that_is_only_a_comment()
    test_rejects_lowercase_and_sample_ids()
    test_rejects_missing_define()
    test_install_fourccs_overwrites_the_raw_line()
    test_install_fourccs_rejects_sample_id()
    test_rejects_acme_pmbl_plist_pair()
    test_resource_scripts_exit_before_writing_a_plist()
    print(check_resources(ROOT))


if __name__ == "__main__":
    try:
        main()
    except PlugIdentityError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
