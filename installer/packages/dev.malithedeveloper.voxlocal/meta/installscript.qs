function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType !== "windows")
        return;

    var appData = installer.environmentVariable("APPDATA");
    if (appData === "")
        return;

    var stalePlugin = appData + "/obs-studio/plugins/voxlocal";
    var selectedTarget = installer.value("TargetDir").replace(/\\/g, "/").toLowerCase();
    var staleParent = (appData + "/obs-studio/plugins").replace(/\\/g, "/").toLowerCase();
    if (selectedTarget === staleParent)
        return;

    var systemRoot = installer.environmentVariable("SystemRoot");
    var command = (systemRoot !== "" ? systemRoot : "C:/Windows") + "/System32/cmd.exe";
    component.addOperation("Execute", command, "/D", "/S", "/C",
                           "if exist \"" + stalePlugin + "\" rmdir /S /Q \"" + stalePlugin + "\"");
}
