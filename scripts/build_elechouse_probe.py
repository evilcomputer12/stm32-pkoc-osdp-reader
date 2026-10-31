"""Build pinned vendor sources for an isolated NFC comparison.

I2C is unchanged. HSU needs explicit byte casts for STM32duino 3's Arduino
HardwareSerial base; only a generated build copy is changed, not the checkout.
"""
from pathlib import Path
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
vendor = root / ".deps/elechouse-pn532"
framework = Path(env.PioPlatform().get_package_dir("framework-arduinoststm32"))
transport = "PN532_HSU" if env.GetProjectOption("custom_elechouse_transport", "i2c") == "hsu" else "PN532_I2C"
env.Prepend(CPPPATH=[str(vendor / "PN532"), str(vendor / transport),
                    str(framework / "libraries/Wire/src")])
for component in ("PN532", transport):
    source_dir = vendor / component
    if component == "PN532_HSU":
        source = (source_dir / "PN532_HSU.cpp").read_text()
        for value, expected in (("0", 3), ("PN532_PREAMBLE", 1),
                                ("PN532_STARTCODE1", 1), ("PN532_POSTAMBLE", 1)):
            old = "_serial->write(" + value + ");"
            assert source.count(old) == expected, "Pinned vendor source changed"
            source = source.replace(old, "_serial->write(uint8_t(" + value + "));")
        source_dir = root / env.subst("$BUILD_DIR") / "vendor_source" / component
        source_dir.mkdir(parents=True, exist_ok=True)
        generated = source_dir / "PN532_HSU.cpp"
        if not generated.exists() or generated.read_text() != source:
            generated.write_text(source)
    env.BuildSources(env.subst("$BUILD_DIR/vendor/" + component), str(source_dir),
                     src_filter=["-<*>", "+<" + component + ".cpp>"])
