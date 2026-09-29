#!/bin/bash

export PATH=~/Qt/5.11.3/gcc_64/bin/:$PATH

# IFCConvert_de.ts is merged in: the plugin only installs ImportIFCPlugin_de.qm
lrelease ImportIFCPlugin_de.ts ../../../IFCConvert/resources/translations/IFCConvert_de.ts -qm ImportIFCPlugin_de.qm

