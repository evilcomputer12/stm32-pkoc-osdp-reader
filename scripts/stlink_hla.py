"""Use the ST-Link HLA path verified with this V2 probe; dapdirect wedged it."""
Import("env")
env.BoardConfig().update("debug.tools.stlink.server.arguments", [
    "-s", "$PACKAGE_DIR/openocd/scripts",
    "-f", "interface/stlink-hla.cfg", "-c", "transport select hla_swd",
    "-f", "target/stm32f1x.cfg",
])
