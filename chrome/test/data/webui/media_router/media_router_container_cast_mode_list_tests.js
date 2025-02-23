// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/** @fileoverview Suite of tests for media-router-container that focus on the
 * cast mode list.
 */
cr.define('media_router_container_cast_mode_list', function() {
  function registerTests() {
    suite('MediaRouterContainerCastModeList', function() {
      /**
       * Checks whether |view| matches the current view of |container|.
       *
       * @param {!media_router.MediaRouterView} view Expected view type.
       */
      var checkCurrentView;

      /**
       * Checks whether the elements specified in |elementIdList| are visible.
       * Checks whether all other elements are not visible. Throws an assertion
       * error if this is not true.
       *
       * @param {!Array<!string>} elementIdList List of id's of elements that
       *     should be visible.
       */
      var checkElementsVisibleWithId;

      /**
       * Checks whether |expected| and the text in the |element| are equal.
       *
       * @param {!string} expected Expected text.
       * @param {!Element} element Element whose text will be checked.
       */
      var checkElementText;

      /**
       * Media Router Container created before each test.
       * @type {?MediaRouterContainer}
       */
      var container;

      /**
       * The blocking issue to show.
       * @type {?media_router.Issue}
       */
      var fakeBlockingIssue;

      /**
       * The list of CastModes to show.
       * @type {!Array<!media_router.CastMode>}
       */
      var fakeCastModeList = [];

      /**
       * The list of CastModes to show with non-default modes only.
       * @type {!Array<!media_router.CastMode>}
       */
      var fakeCastModeListWithNonDefaultModesOnly = [];

      /**
       * The blocking issue to show.
       * @type {?media_router.Issue}
       */
      var fakeNonBlockingIssue;

      /**
       * The list of available sinks.
       * @type {!Array<!media_router.Sink>}
       */
      var fakeSinkList = [];

      // Import media_router_container.html before running suite.
      suiteSetup(function() {
        return PolymerTest.importHtml(
            'chrome://media-router/elements/media_router_container/' +
            'media_router_container.html');
      });

      setup(function(done) {
        PolymerTest.clearBody();
        // Initialize a media-router-container before each test.
        container = document.createElement('media-router-container');
        document.body.appendChild(container);

        // Get common functions and variables.
        var test_base = media_router_container_test_base.init(container);

        checkCurrentView = test_base.checkCurrentView;
        checkElementsVisibleWithId = test_base.checkElementsVisibleWithId;
        checkElementText = test_base.checkElementText;
        fakeBlockingIssue = test_base.fakeBlockingIssue;
        fakeCastModeList = test_base.fakeCastModeList;
        fakeCastModeListWithNonDefaultModesOnly =
            test_base.fakeCastModeListWithNonDefaultModesOnly;
        fakeNonBlockingIssue = test_base.fakeNonBlockingIssue;
        fakeSinkList = test_base.fakeSinkList;

        container.castModeList = test_base.fakeCastModeList;

        // Allow for the media router container to be created, attached, and
        // listeners registered in an afterNextRender() call.
        Polymer.RenderStatus.afterNextRender(this, done);
      });

      // Container remains in auto mode even if the cast mode list changed.
      test('cast mode list updated in auto mode', function(done) {
        assertEquals(media_router.AUTO_CAST_MODE.description,
            container.headerText);
        assertEquals(media_router.CastModeType.AUTO,
            container.shownCastModeValue_);
        assertFalse(container.userHasSelectedCastMode_);

        container.castModeList = fakeCastModeList.slice(1);
        setTimeout(function() {
          assertEquals(media_router.AUTO_CAST_MODE.description,
              container.headerText);
          assertEquals(media_router.CastModeType.AUTO,
              container.shownCastModeValue_);
          assertFalse(container.userHasSelectedCastMode_);
          done();
        });
      });

      // Tests that |container| returns to SINK_LIST view and arrow drop icon
      // toggles after a cast mode is selected.
      test('select cast mode', function(done) {
        container.castModeList = fakeCastModeListWithNonDefaultModesOnly;

        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        checkCurrentView(media_router.MediaRouterView.CAST_MODE_LIST);

        setTimeout(function() {
          var castModeList =
              container.$$('#cast-mode-list').querySelectorAll('paper-item');
          MockInteractions.tap(castModeList[2]);
          checkCurrentView(media_router.MediaRouterView.SINK_LIST);
          done();
        });
      });

      // Tests that clicking on the drop down icon will toggle |container|
      // between SINK_LIST and CAST_MODE_LIST views.
      test('click drop down icon', function() {
        checkCurrentView(media_router.MediaRouterView.SINK_LIST);

        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        checkCurrentView(media_router.MediaRouterView.CAST_MODE_LIST);

        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        checkCurrentView(media_router.MediaRouterView.SINK_LIST);
      });

      // Tests the header text. Choosing a cast mode updates the header text.
      test('header text with no default cast modes', function(done) {
        assertEquals(loadTimeData.getString('selectCastModeHeaderText'),
            container.i18n('selectCastModeHeaderText'));

        // The container is currently in auto cast mode, since we have not
        // picked a cast mode explicitly, and the sinks is not compatible
        // with exactly one cast mode.
        assertEquals(media_router.AUTO_CAST_MODE.description,
            container.headerText);
        assertFalse(container.userHasSelectedCastMode_);

        container.castModeList = fakeCastModeListWithNonDefaultModesOnly;

        // Switch to cast mode list view.
        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        setTimeout(function() {
          var castModeList =
              container.$$('#cast-mode-list').querySelectorAll('paper-item');
          assertEquals(fakeCastModeListWithNonDefaultModesOnly.length,
              castModeList.length);
          for (var i = 0; i < castModeList.length; i++) {
            MockInteractions.tap(castModeList[i]);
            assertEquals(
                fakeCastModeListWithNonDefaultModesOnly[i].description,
                container.headerText);
            checkElementText(
                fakeCastModeListWithNonDefaultModesOnly[i].description,
                castModeList[i]);
          }

          done();
        });
      });

      // Tests the header text when updated with a cast mode list with a mix of
      // default and non-default cast modes.
      test('cast modes with one default mode', function(done) {
        container.castModeList = fakeCastModeList;

        // Switch to cast mode list view.
        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        setTimeout(function() {
          var castModeList =
              container.$$('#cast-mode-list').querySelectorAll('paper-item');

          for (var i = 0; i < fakeCastModeList.length; i++) {
            MockInteractions.tap(castModeList[i]);
            if (fakeCastModeList[i].type ==
                media_router.CastModeType.DEFAULT) {
              assertEquals(fakeCastModeList[i].description,
                  container.headerText);

              checkElementText(fakeCastModeList[i].host, castModeList[i]);
            } else {
              assertEquals(fakeCastModeList[i].description,
                  container.headerText);
              checkElementText(fakeCastModeList[i].description,
                  castModeList[i]);
            }
          }

          done();
        });
      });

      // Tests for expected visible UI when the view is CAST_MODE_LIST.
      test('cast mode list state visibility', function(done) {
        container.showCastModeList_();
        setTimeout(function() {
          checkElementsVisibleWithId(['cast-mode-list',
                                      'container-header',
                                      'device-missing']);

          // Set a non-blocking issue. The issue should stay hidden.
          container.issue = fakeNonBlockingIssue;
          setTimeout(function() {
            checkElementsVisibleWithId(['cast-mode-list',
                                        'container-header',
                                        'device-missing']);

            // Set a blocking issue. The issue should stay hidden.
            container.issue = fakeBlockingIssue;
            setTimeout(function() {
              checkElementsVisibleWithId(['container-header',
                                          'device-missing',
                                          'issue-banner']);
              done();
            });
          });
        });
      });

      // If the container is not in auto mode, and the mode it is currently in
      // no longer exists in the list of cast modes, then switch back to auto
      // mode.
      test('cast mode list updated in selected cast mode', function(done) {
        assertEquals(media_router.AUTO_CAST_MODE.description,
            container.headerText);
        assertEquals(media_router.CastModeType.AUTO,
            container.shownCastModeValue_);
        assertFalse(container.userHasSelectedCastMode_);

        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        setTimeout(function() {
          var castModeList =
                container.$$('#cast-mode-list')
                    .querySelectorAll('paper-item');
          MockInteractions.tap(castModeList[0]);
          setTimeout(function() {
            assertEquals(fakeCastModeList[0].description,
                container.headerText);
            assertEquals(fakeCastModeList[0].type,
                container.shownCastModeValue_);
            assertTrue(container.userHasSelectedCastMode_);
            container.castModeList = fakeCastModeList.slice(1);
            setTimeout(function() {
              assertEquals(media_router.AUTO_CAST_MODE.description,
                  container.headerText);
              assertEquals(media_router.CastModeType.AUTO,
                  container.shownCastModeValue_);
              assertFalse(container.userHasSelectedCastMode_);
              done();
            });
          });
        });
      });

      // Tests that after a different cast mode is selected, the sink list will
      // change based on the sinks compatibility with the new cast mode.
      test('changing cast mode changes sink list', function(done) {
        container.allSinks = fakeSinkList;

        MockInteractions.tap(container.$['container-header'].
            $['arrow-drop-icon']);
        setTimeout(function() {
          var castModeList =
                container.$$('#cast-mode-list')
                    .querySelectorAll('paper-item');
          MockInteractions.tap(castModeList[0]);
          assertEquals(fakeCastModeList[0].description,
              container.headerText);

          setTimeout(function() {
            var sinkList =
                container.shadowRoot.getElementById('sink-list')
                    .querySelectorAll('paper-item');

            // The sink list is empty because none of the sinks in
            // fakeSinkList is compatible with cast mode 0.
            assertEquals(0, sinkList.length);
            MockInteractions.tap(castModeList[2]);
            assertEquals(fakeCastModeList[2].description,
                container.headerText);

            setTimeout(function() {
              var sinkList =
                  container.shadowRoot.getElementById('sink-list')
                      .querySelectorAll('paper-item');
              assertEquals(3, sinkList.length);
              done();
            });
          });
        });
      });

      // Tests that the sink list does not contain any sinks that are not
      // compatible with the selected cast mode and are not associated with a
      // route.
      test('sink list in user selected cast mode', function(done) {
        var newSinks = [
          new media_router.Sink('sink id 10', 'Sink 10', null, null,
              media_router.SinkIconType.CAST,
              media_router.SinkStatus.ACTIVE, 0x4 | 0x8),
          new media_router.Sink('sink id 20', 'Sink 20', null, null,
              media_router.SinkIconType.CAST,
              media_router.SinkStatus.ACTIVE, 0x2 | 0x4 | 0x8),
          new media_router.Sink('sink id 30', 'Sink 30', null, null,
              media_router.SinkIconType.CAST,
              media_router.SinkStatus.PENDING, 0x4 | 0x8),
        ];

        container.allSinks = newSinks;
        container.routeList = [
          new media_router.Route('id 1', 'sink id 30',
                                 'Title 1', 1, false, false),
        ];

        setTimeout(function() {
          var sinkList =
              container.shadowRoot.getElementById('sink-list')
                  .querySelectorAll('paper-item');

          // Since we haven't selected a cast mode, we don't filter sinks.
          assertEquals(3, sinkList.length);

          MockInteractions.tap(container.$['container-header'].
              $['arrow-drop-icon']);
          setTimeout(function() {
            // Cast mode 1 is selected, and the sink list is filtered.
            var castModeList =
                container.$$('#cast-mode-list').querySelectorAll('paper-item');
            MockInteractions.tap(castModeList[1]);
            assertEquals(fakeCastModeList[1].description,
                container.headerText);
            assertEquals(fakeCastModeList[1].type,
                container.shownCastModeValue_);

            setTimeout(function() {
              var sinkList =
                  container.shadowRoot.getElementById('sink-list')
                      .querySelectorAll('paper-item');

              // newSinks[0] got filtered out since it is not compatible with
              // cast mode 1.
              // 'Sink 20' should be on the list because it contains the
              // selected cast mode. (sinkList[0] = newSinks[1])
              // 'Sink 30' should be on the list because it has a route.
              // (sinkList[1] = newSinks[2])
              assertEquals(2, sinkList.length);
              checkElementText(newSinks[1].name, sinkList[0]);

              // |sinkList[1]| contains route title in addition to sink name.
              assertTrue(sinkList[1].textContent.trim().startsWith(
                  newSinks[2].name.trim()));

              // Cast mode is not switched back even if there are no sinks
              // compatible with selected cast mode, because we explicitly
              // selected that cast mode.
              container.allSinks = [];
              setTimeout(function() {
                assertEquals(fakeCastModeList[1].description,
                    container.headerText);
                assertEquals(fakeCastModeList[1].type,
                    container.shownCastModeValue_);
                var sinkList =
                    container.shadowRoot.getElementById('sink-list')
                        .querySelectorAll('paper-item');
                assertEquals(0, sinkList.length);
                done();
              });
            });
          });
        });
      });
    });
  }

  return {
    registerTests: registerTests,
  };
});
