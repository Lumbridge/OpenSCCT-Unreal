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
}
