# How Tools/Test/Run-ScctTests.ps1 runs each named client test (Source/OpenSCCT/Test). A test not listed here runs with
# the defaults: no map, no URL options, no extra switches, a 120 s timeout and no tags.
#
#   Map      -ScctTestMap: the map a test that plays "a map" uses (play-exit picks it on the front end's offline page)
#   Options  -ScctTestOptions: URL options added to every level the test opens ("?Bots=1?Side=merc")
#   Extra    more switches for the command line, as written
#   Timeout  seconds the test may run (-ScctTestTimeout); the process is killed 120 s after that
#   Tags     needs-content  needs the converted content folder (-Content); skipped without it
#            fixture        needs a fixture level of Content/OpenSCCT/Test (Fixture names its .umap); skipped without it
#            needs-maps     needs Map's imported level under Content/Imported/Maps; skipped without it
#            windowed-only  only with -Windowed (the picture is what it checks)
#            helper         started by another test, never on its own (-All leaves it out)
#   Fixture  the fixture level a 'fixture' test needs, relative to the project
#
# The expected failures (-ExpectedFailures, default selftest-fail) must fail with exit code 1; they need no tag.
@{
    'selftest-pass' = @{ Timeout = 30 }
    'selftest-fail' = @{ Timeout = 30 }
    'boot'          = @{ Timeout = 120; Tags = @('needs-content', 'fixture'); Fixture = 'Content/OpenSCCT/Test/TestBox.umap' }
    'testbox'       = @{ Timeout = 180; Tags = @('needs-content', 'fixture'); Fixture = 'Content/OpenSCCT/Test/TestBox.umap' }
    'play-exit'     = @{ Map = 'AquaE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    # The frame-time tests (Test/ScctFrameTests.cpp); each writes Saved/Test/frames-<name>.csv. Vertical sync is only
    # judged with -Windowed.
    'frame-rate'    = @{ Map = 'AquaE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'smooth'        = @{ Map = 'AquaE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-hitch'    = @{ Map = 'AquaE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'first-use'     = @{ Map = 'AquaE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    # The play inspector (Test/ScctPlayInspectTest.cpp): about 40 s at the keys with three bots, failed on any issue of
    # its areas (animation, feet, camera, audio, net, HUD, pause menu, input). -Extra '-ScctInspectShots' saves a picture
    # at each step to Saved/ScctShots/<test>/; -Extra '-ScctTestFreezePose' stops every body's pose (Animation must fail).
    'play-inspect'      = @{ Map = 'AquaE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-inspect-merc' = @{ Map = 'AquaE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    # The netcode checks (Test/ScctNetcodeTests.cpp): SteeE over a simulated 100/200 ms link with jitter, loss,
    # duplication and reordering; listen or dedicated server, tuned or stock net profile, walk, grab or co-op.
    # -Extra '-ScctTestFrames=10' saves a 10 fps picture sequence to Saved/ScctTestFrames/<test>/. The 300 ms ones are
    # the Stress300 link, run by hand (only the link is judged there).
    'play-net-listen100'          = @{ Map = 'SteeE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-net-listen200'          = @{ Map = 'SteeE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-net-listen100-stock'    = @{ Map = 'SteeE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-net-listen200-stock'    = @{ Map = 'SteeE'; Timeout = 240; Tags = @('needs-content', 'needs-maps') }
    'play-net-dedicated100'       = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-dedicated200'       = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-dedicated100-stock' = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-dedicated200-stock' = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-grab100'            = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-grab200'            = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-grab100-stock'      = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-coop100'            = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-coop200'            = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps') }
    'play-net-listen300'          = @{ Map = 'SteeE'; Timeout = 240; Tags = @('needs-content', 'needs-maps', 'helper') }
    'play-net-dedicated300'       = @{ Map = 'SteeE'; Timeout = 300; Tags = @('needs-content', 'needs-maps', 'helper') }
}