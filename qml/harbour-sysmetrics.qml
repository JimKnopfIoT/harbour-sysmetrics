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

    // Root-helper switch. Deliberately NOT re-applied on launch: the helper
    // stops itself once the app is gone, so root mode is per session and the
    // user grants it in the moment, rather than a stored flag granting it
    // silently at every start.
    ConfigurationValue {
        id: cfgRootHelper
        key: "/apps/harbour-sysmetrics/rootHelperEnabled"
        defaultValue: false
    }
    Component.onCompleted: {
        sysmon.foreground = applicationActive
        cfgRootHelper.value = false
    }
}
