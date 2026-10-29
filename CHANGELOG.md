# Changelog

## 1.0.0 (2026-10-29)


### Features

* add real-time remote telemetry streaming and control plane ([196126c](https://github.com/zkalves/condor/commit/196126c7b095a846df44d1b6dac7897916f9a0ef))
* add schema validation, matrix sweeps, count multipliers, and 3-tier defaults ([bd53c64](https://github.com/zkalves/condor/commit/bd53c643b746a5375ed66f21b798e90b5dc9a36a))
* add stage/job hooks, HTML report links, unified defaults, and settings backend ([41b7748](https://github.com/zkalves/condor/commit/41b7748bb87ed24f21b928dc477c2f527a3d9198))
* add workflow improvements for EDA and verification workloads ([8268daa](https://github.com/zkalves/condor/commit/8268daaae2a359156ca613b135e680c4cb00709f))
* **assets:** replace project logo with res/images/icon.svg ([95acce8](https://github.com/zkalves/condor/commit/95acce8a73617ad8c60da83a8f9baa8a338d5178))
* **backends:** remove hardcoded backends, add dynamic discovery and support for LSF, SGE/Grid Engine, PBS/TORQUE, and Spark ([0cba301](https://github.com/zkalves/condor/commit/0cba301e0e3332ea89f2922769b16e7544c360cb))
* **cli:** add -v and --version flags to CLI ([764c8ff](https://github.com/zkalves/condor/commit/764c8ff30ec1d26576b3820205e8b24cbb221ca3))
* **cli:** display available keys and emergency kill command for crashed CLI ([41e37ab](https://github.com/zkalves/condor/commit/41e37ab61bd9edefb1a2a43e95b899323f390955))
* **hub:** add start time column and sorting to fleet table ([9da1d01](https://github.com/zkalves/condor/commit/9da1d01c21be58cacb439b6b0f71277512893f13))
* **hub:** compact fleet table with sorting, column filters, and regression fields ([5d27e57](https://github.com/zkalves/condor/commit/5d27e57caf7c2757c099461f135d1f7527007b80))
* **hub:** store and serve client HTML reports with configurable artifacts directory ([b0851f2](https://github.com/zkalves/condor/commit/b0851f2e5b7cce6dbb7f017ee833756930b6ea6a))
* implement high-concurrency C++20 Condor Hub server with live web dashboard ([3779e1a](https://github.com/zkalves/condor/commit/3779e1aeffa8d3a78e2ed81cf174df58951c90aa))
* **make:** add build-server, run-server, and run-remote Makefile targets ([b960519](https://github.com/zkalves/condor/commit/b96051981b2ff06fd2fdc7f602c0fa7ca672c9ed))
* **make:** add run-verilator-remote and run-verilator-coverage-remote targets ([9048604](https://github.com/zkalves/condor/commit/90486046bb20276d667eea2b75f661b649f52666))
* **progress:** align DAG pipeline progress bar and add active spinner animation ([c774d54](https://github.com/zkalves/condor/commit/c774d549d1be63fafce4b02ad309a42d339524c4))


### Bug Fixes

* **ci:** fix package tarball extraction, streamline verilator installation, and stabilize workflows ([b4acd24](https://github.com/zkalves/condor/commit/b4acd2465ec08f5bb1434a4d09b9abede04d00b4))
* **cli:** disable terminal echo during live progress rendering ([3d47163](https://github.com/zkalves/condor/commit/3d471633bdeae66e8b22c4b658308ac7cdd88f6b))
* **docs:** resolve &lt;tt&gt; raw tag leakage in headings and enhance inline code styling ([1efdce8](https://github.com/zkalves/condor/commit/1efdce8c5dd9a8502dbb12967d87937fe586245f))
* **hub:** preserve terminal PASSED status on clean disconnect and accurately compute progress ([e5f5343](https://github.com/zkalves/condor/commit/e5f5343a385494eface7af52d5db4bd7b605652c))
* **hub:** resolve deadlock on SIGINT by decoupling signal handler and adding eventfd wakeup ([2031131](https://github.com/zkalves/condor/commit/20311315d3665a1e352c72c28a4b709c76ef91a9))
* **menu:** capture write return value and enforce -Werror on condor targets ([27988d1](https://github.com/zkalves/condor/commit/27988d14df6f79145fb98c7eb1e8ebda4c0d7906))
* Updated icon ([abdbaec](https://github.com/zkalves/condor/commit/abdbaec55df0de323cb4deb1d4835e3a720c559b))
* **verilator:** enforce coverage_file omission when coverage is disabled ([ebcb87b](https://github.com/zkalves/condor/commit/ebcb87b6723c50e5e8794a5156106eb6bf5aa9ef))


### Code Refactoring

* **src:** organize all sources under src/ and add dynamic table dashboard ([9a69f8f](https://github.com/zkalves/condor/commit/9a69f8f1d7f09a8344da6cc5fdcc3f7fdd20dc9b))
* **workspace:** relocate kill_jobs.sh and active_pids.txt to pipeline folder and document crash recovery ([ca1a870](https://github.com/zkalves/condor/commit/ca1a87070c276dac28d59d32f2088fb7ccae8c3c))
* **workspace:** reorganize run directories into artifacts, pipeline, and stages ([0a8b3e6](https://github.com/zkalves/condor/commit/0a8b3e6af14cf062433c1c4072f6bddd82fe3842))


### Documentation

* document --server-owner CLI option and ignore runtime hub_artifacts/ directory ([6515e93](https://github.com/zkalves/condor/commit/6515e93f020031e7c4a2cbfa7ed296da014ddc84))
* document Condor Hub architecture, server CLI, dashboard, and make targets ([20bffa0](https://github.com/zkalves/condor/commit/20bffa095eec8a57d4160c8d6b20d51be0159ad3))


### Continuous Integration

* Add CODEOWNERS file ([853200a](https://github.com/zkalves/condor/commit/853200a7f4e23f8b33704833ee6572f96fa5966d))
* add modular github actions workflows and release-please configuration ([ef809dc](https://github.com/zkalves/condor/commit/ef809dc95a603d78b2167ca8d8b69355e5081acf))
* bump upload-artifact and download-artifact to v7 targeting node 24 natively ([8830799](https://github.com/zkalves/condor/commit/8830799e6510d73400015ff2df198cc10824b436))
* configure release-please for v1.0.0 bootstrap and add manual dispatch ([23f2ddf](https://github.com/zkalves/condor/commit/23f2ddf91c3703fa34ce6bfeba2374ff9e3f86cd))
* upgrade github actions to v5 targeting node 24 ([137acaf](https://github.com/zkalves/condor/commit/137acaf0d5019fd2e06f55e8858f39284d004334))
