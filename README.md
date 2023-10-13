infoviewer
----------

View all kinds of information.
Sources can be mqtt, output from a program (one-shot or tail) and static texts.
Data from sources can be evaluated if it is json.
The layout of the screen is fully customizable.


build
-----

packages required:
* libsdl2-dev
* libsdl2-gfx-dev
* libsdl2-image-dev
* libsdl2-ttf-dev
* libmosquitto-dev
* libconfig++-dev
* libjansson-dev

The example uses fonts from fonts-freefont-ttf

"infoviewer.cfg" is the configuration I use at home (see screenshot below). It has local dependencies and shows all functionality.
With regular internet access you should be able to give "example.cfg" a try.


how to run
----------

infoviewer example.cfg


![(screenshot)](images/schermpje3.jpg)


License: CC0

written by folkert van heusden <mail@vanheusden.com>
