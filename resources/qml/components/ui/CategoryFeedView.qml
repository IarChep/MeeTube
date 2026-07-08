import QtQuick 1.1
import com.nokia.meego 1.0
import "../delegates"
import "../../js/UIConstants.js" as UI
import "../../js/Status.js" as Status

// One category's page inside the swipeable pager (MainPage): a vertical video list bound
// to `feed` (a VideoModel, or null), with the usual loading / empty / error overlays. When
// `locked` (signed-out on Home) it shows the LoginPrompt instead of a feed. The parent owns
// the model (see MainPage.modelFor) and handles login()/retry(). Sibling ui/ components
// (BusyOverlay, EmptyState, ListFooter, LoginPrompt) resolve without an import.
Item {
    id: root

    property variant feed: null
    property bool locked: false
    signal login
    signal retry

    ListView {
        id: list
        anchors.fill: parent
        clip: true
        cacheBuffer: 1000
        visible: !root.locked
        model: root.feed
        delegate: VideoDelegate {}

        // Animated "loading more" footer for infinite scroll; collapses to 0 at the end.
        footer: ListFooter {
            hasMore: root.feed ? root.feed.canFetchMore : false
            active: root.feed ? (root.feed.status === Status.Loading) : false
        }

        // Infinite scroll: pull the next page when the list bottoms out.
        onAtYEndChanged: {
            if (atYEnd && root.feed && root.feed.canFetchMore)
                root.feed.fetchMore();
        }

        ScrollDecorator { flickableItem: list }
    }

    // First-load spinner (only when there's nothing to show yet). Never on the login gate.
    BusyOverlay {
        running: !root.locked
                 && (!root.feed
                     || (root.feed.status === Status.Loading && root.feed.count === 0))
        text: "Loading videos…"
    }

    // Signed-out + Home: the login call-to-action stands in for the empty anonymous feed.
    LoginPrompt {
        visible: root.locked
        onLogin: root.login()
    }

    // Empty (loaded but no rows) / error state, with Retry on failure.
    EmptyState {
        property bool failed: root.feed ? (root.feed.status === Status.Failed) : false
        visible: root.locked
                 ? false
                 : (root.feed
                    ? (root.feed.count === 0
                       && (root.feed.status === Status.Ready || failed))
                    : false)
        title: failed ? "Couldn't load videos" : "Nothing here yet"
        hint: (failed && root.feed) ? root.feed.errorString : "Try another category."
        showRetry: failed
        onRetry: root.retry()
    }
}
