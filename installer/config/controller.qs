function Controller()
{
    installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.Yes);

    var home = installer.value("HomeDir");
    var platform = installer.value("os");
    var target = "";

    if (platform === "win") {
        var programData = installer.environmentVariable("PROGRAMDATA");
        target = (programData !== "" ? programData : "C:/ProgramData")
            + "/obs-studio/plugins";
    } else if (platform === "mac") {
        target = home + "/Library/Application Support/obs-studio/plugins";
    } else {
        var configHome = installer.environmentVariable("XDG_CONFIG_HOME");
        target = (configHome !== "" ? configHome : home + "/.config")
            + "/obs-studio/plugins";
    }

    installer.setValue("TargetDir", target);
}

Controller.prototype.IntroductionPageCallback = function()
{
    var widget = gui.currentPageWidget();
    if (widget !== null) {
        widget.MessageLabel.setText(
            "This setup installs VoxLocal for OBS Studio 32.2.1 or newer. "
            + "Close OBS Studio before continuing."
        );
    }
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    var widget = gui.currentPageWidget();
    if (widget !== null) {
        widget.title = "Choose the OBS plugin folder";
        if (installer.value("os") === "win") {
            widget.MessageLabel.setText(
                "OBS Studio 32.2.1's system plugin folder is selected automatically. "
                + "The final folder must be ProgramData/obs-studio/plugins."
            );
        } else {
            widget.MessageLabel.setText(
                "The OBS plugin folder is selected automatically. "
                + "Change it only for a portable or custom OBS installation."
            );
        }
    }
}
