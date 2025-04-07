buildDebSbuild(
    defaultTargets: 'trixie-armhf trixie-arm64 trixie-host',
    defaultWbdevImage: 'registry.wirenboard.lan/contactless/devenv_test:trixie',
    repos: ['release', 'devTools'],
    defaultRunLintian: true,
    customBuildSteps: {
        stage("Build win32") {
            dir("$PROJECT_SUBDIR") {
                sh 'wbdev root bash -c "apt-get update && apt-get -y install gcc-mingw-w64-i686 && unset CC && make -f Makefile.win32"'
            }
            sh "mv $PROJECT_SUBDIR/*.exe $RESULT_SUBDIR/"
            archiveArtifacts artifacts: "$RESULT_SUBDIR/*.exe"
        }
    }
)
