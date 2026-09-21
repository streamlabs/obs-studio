OBS Studio <https://obsproject.com>
===================================

.. image:: https://github.com/obsproject/obs-studio/actions/workflows/push.yaml/badge.svg?branch=master
   :alt: OBS Studio Build Status - GitHub Actions
   :target: https://github.com/obsproject/obs-studio/actions/workflows/push.yaml?query=branch%3Amaster

.. image:: https://badges.crowdin.net/obs-studio/localized.svg
   :alt: OBS Studio Translation Project Progress
   :target: https://crowdin.com/project/obs-studio

.. image:: https://img.shields.io/discord/348973006581923840.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2
   :alt: OBS Studio Discord Server
   :target: https://obsproject.com/discord

What is OBS Studio?
-------------------

OBS Studio is software designed for capturing, compositing, encoding,
recording, and streaming video content, efficiently.

It's distributed under the GNU General Public License v2 (or any later
version) - see the accompanying COPYING file for more details.

Quick Links
-----------

- Website: https://obsproject.com

- Help/Documentation/Guides: https://github.com/obsproject/obs-studio/wiki

- Forums: https://obsproject.com/forum/

- Build Instructions: https://github.com/obsproject/obs-studio/wiki/Install-Instructions

- Developer/API Documentation: https://obsproject.com/docs

- Donating/backing/sponsoring: https://obsproject.com/contribute

- Bug Tracker: https://github.com/obsproject/obs-studio/issues

Native Tests
------------

Native libobs tests use Catch2 v3.11.0 and CTest. The basic cases cover OBS
startup/shutdown, settings objects, scenes, arrays, serialization, bitstream
reads, and path parsing without rendering video or loading plugins. Windows
also has temporary-file tests for concurrent writers and rejection of junctions
and symbolic links. The normal OBS build dependencies are required; CMake
downloads Catch2 on the first configure unless it is already cached.

Run the following commands from the repository root on Windows to configure,
build, and run all currently registered tests, including the existing x264
options-parser test:

.. code-block:: powershell

   cmake --preset windows-ci-x64
   cmake --build build_x64 --config RelWithDebInfo --target libobs_unit_tests obs-x264-test
   ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure --no-tests=error

To run only the Catch2 suite, add ``-R "^libobs_unit_tests::"`` to the CTest
command. To run only the OBS startup/shutdown case:

.. code-block:: powershell

   ctest --test-dir build_x64 -C RelWithDebInfo -R "^libobs_unit_tests::OBS initializes and shuts down$" --output-on-failure --no-tests=error

On macOS, build and run the Catch2 suite with:

.. code-block:: console

   cmake --preset macos-ci
   cmake --build build_macos --config RelWithDebInfo --target libobs_unit_tests
   ctest --test-dir build_macos -C RelWithDebInfo -R "^libobs_unit_tests::" --output-on-failure --no-tests=error

The ``windows-ci-x64`` and ``macos-ci`` presets enable ``BUILD_TESTING``.
It defaults to ``OFF`` for other presets; pass ``-DBUILD_TESTING=ON`` when
configuring to enable it explicitly. CTest does not build tests, so repeat
the build step after code changes. Test executables and Catch2 are excluded
from installed product packages.

See `test/libobs/README.md <test/libobs/README.md>`_ for details about the
suite and adding cases.

Contributing
------------

- If you would like to help fund or sponsor the project, you can do so
  via `Patreon <https://www.patreon.com/obsproject>`_, `OpenCollective
  <https://opencollective.com/obsproject>`_, or `PayPal
  <https://www.paypal.me/obsproject>`_.  See our `contribute page
  <https://obsproject.com/contribute>`_ for more information.

- If you wish to contribute code to the project, please make sure to
  read the coding and commit guidelines:
  https://github.com/obsproject/obs-studio/blob/master/CONTRIBUTING.rst

- Developer/API documentation can be found here:
  https://obsproject.com/docs

- If you wish to contribute translations, do not submit pull requests.
  Instead, please use Crowdin.  For more information read this page:
  https://obsproject.com/wiki/How-To-Contribute-Translations-For-OBS

- Contributors to OBS Studio and related repositories are expected to
  follow our Code of Conduct, which can be read here:
  https://github.com/obsproject/obs-studio/blob/master/COC.rst

- Other ways to contribute are by helping people out with support on
  our forums or in our community chat.  Please limit support to topics
  you fully understand -- bad advice is worse than no advice.  When it
  comes to something that you don't fully know or understand, please
  defer to the official help or official channels. 


SAST Tools
----------

`PVS-Studio <https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source>`_ - static analyzer for C, C++, C#, and Java code.
