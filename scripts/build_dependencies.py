"""Compile only the required roles from the pinned, unmodified sources."""
from pathlib import Path
Import("env")

root = Path(env.subst("$PROJECT_DIR"))
env.Append(CCFLAGS=["-Os"])
env.Append(CFLAGS=["-std=c11"])
env.Append(LINKFLAGS=["-Wl,-Map," + env.subst("$BUILD_DIR/firmware.map")])
env.Append(CPPPATH=[str(root / "include")])
components = env.GetProjectOption("custom_components", "osdp").split(",")
if "asymcred" in components:
    base = root / ".deps/asymcred"
    env.Append(CPPPATH=[str(base / p) for p in ("core/include", "pkoc/include")])
    env.BuildSources(env.subst("$BUILD_DIR/dependencies/asymcred"), str(base),
                     src_filter=["-<*>", "+<core/src/>", "+<pkoc/src/>"])
if "osdp" in components:
    osdp = root / ".deps/osdp-embedded"
    # The pinned public C header names a parameter `template`, a C++ keyword.
    # Generate a one-token compatibility overlay; never edit the dependency cache.
    header = (osdp / "pd/include/osdp/osdp_pd.h").read_text()
    old = "osdp_pd_pdcap_template_t template,"
    assert header.count(old) == 1, "Review OSDP C++ compatibility after dependency updates"
    overlay = Path(env.subst("$BUILD_DIR")) / "compat/osdp"
    overlay.mkdir(parents=True, exist_ok=True)
    (overlay / "osdp_pd.h").write_text(header.replace(old, "osdp_pd_pdcap_template_t selector,"))
    env.Prepend(CPPPATH=[str(overlay.parent)])
    env.Append(CPPPATH=[str(osdp / p) for p in
        ("core/include", "core/src", "pd/include", "pd/src")])
    env.BuildSources(env.subst("$BUILD_DIR/dependencies/osdp"), str(osdp),
                     src_filter=["-<*>", "+<core/src/shared/>", "+<core/src/sc/>",
                                 "+<core/src/commands/>", "+<core/src/replies/>",
                                 "+<pd/src/>"])

if "atecc" in components:
    base = root / ".deps/cryptoauthlib/lib"
    env.Append(CPPPATH=[str(base)])
    env.BuildSources(env.subst("$BUILD_DIR/dependencies/atecc"), str(base),
        src_filter=["-<*>", "+<*.c>", "+<calib/*.c>", "+<host/*.c>",
                    "+<crypto/*.c>", "+<crypto/hashes/*.c>", "+<hal/atca_hal.c>"])

if "wolfcrypt" in components:
    base = root / ".deps/wolfssl"
    env.Append(CPPPATH=[str(base), str(root / ".deps/osdp-embedded/ports/wolfcrypt"), str(root / "config")])
    sources = ("aes", "sha256", "hash", "random", "wc_port", "wc_encrypt", "error", "memory", "logging")
    env.BuildSources(env.subst("$BUILD_DIR/dependencies/wolfcrypt"), str(base / "wolfcrypt/src"),
                     src_filter=["-<*>"] + [f"+<{name}.c>" for name in sources])
    if "osdp" in components:
        env.BuildSources(env.subst("$BUILD_DIR/dependencies/osdp_wolfcrypt"),
                         str(root / ".deps/osdp-embedded/ports/wolfcrypt"))
