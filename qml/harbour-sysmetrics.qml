import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import "pages"
import "cover"

ApplicationWindow {
    id: app
    initialPage: Component { MainPage {} }
    cover: Component { CoverPage {} }
    allowedOrientations: defaultAllowedOrientations

    // Covered: the process list is out of sight, so stop paying for it. The
    // cover's own figures come from the cheap system sample and keep running.
    onApplicationActiveChanged: sysmon.foreground = applicationActive

    // Root-helper switch, re-applied on launch (default off).
    ConfigurationValue {
        id: cfgRootHelper
        key: "/apps/harbour-sysmetrics/rootHelperEnabled"
        defaultValue: false
    }
    Component.onCompleted: {
        sysmon.foreground = applicationActive
        if (cfgRootHelper.value)
            rootmon.setHelper(true)
    }
}
