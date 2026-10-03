# Use as a template
# Copy to "Build_Config_Local.ps1"

# Set up Visual Studio 2022 x64 environment
$vsDevShellPath = "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/Launch-VsDevShell.ps1"

# Papyrus compiler, when this checkout has no plugins/external/papyrus-compiler (a git worktree):
# point it at another checkout's copy. Empty: the checkout's own (DownloadExternalDeps.ps1).
$papyrusCompilerPath = ""
