This readme will try to describe how to use HermesIntf and CWSL_Tee together to
be able to skim up to 16 receivers if the hardware is capable, run other SDR
apps together with them, and select which ADC input each receiver gets
(hardware permitting).

Currently I know of only two thta support greater than 8 receivers. The Hermes-Lite
with the Bemicro CVA9 (large FPGA) and the ANAN-200D or Orion HPSDR with
my (N1GP) modified firmware.

* NOTE: The Orion has two ADCs, I added provisions to select which and will describe
further down (step 14.) how to select which.

Bob (N6TV) described how to get two instances of SkimSrv on the same machine
on the http://dayton.contesting.com/pipermail/skimmertalk group.

I'll use his description but with a few changes. He was describing two instances
that shared 8 receivers, mine you can use up to 16 so the 1st instance could skim say:

136750,475000,1885000,3585000,5417000,7085000,10185000,14085000

and the second:

18153000,21085000,24975000,28085000,28255000,50085000,50255000

Plus if your PC can handle it, you can also have RttySkimSrv use any of those bands
for 3 skimmers, one PC, 16 bands (see step 11. for that).

To run two instances of SkimSrv on the same machine, I had to do the
following:

   1. Install CW Skimmer Server in the default location (*C:\Program Files
   (x86)\Afreet\SkimSrv\*)

   2. Copy *HermesIntf.dll* to the same location (Administrator privileges
   required)

   3. Copy *CWSL_Tee.dll* to the same location (Administrator privileges
   required)

   4. Copy *C:\Program Files (x86)\Afreet\SkimSrv\* to *C:\Program Files
   (x86)\Afreet\SkimSrv2\*
   (Copy the *entire folder*; Administrator privileges required)
   -or-
   Install a second copy of CW Skimmer Server, but change the installation
   directory to SkimSrv2 (I did not try this method)

   5. VERY IMPORTANT:  In the new *SkimSrv2* folder, rename *SkimSrv.exe*
   to *SkimSrv2.exe *

   6. Also in *SkimSrv2*, remove HermesIntf.dll or rename it

   7. In SkimSrv2\SkimSrv.ini, change:
   Port=7300
   to
   Port=7302

   8. If required, manually create a new Windows shortcut to the new
   SkimSrv2\SkimSrv2.exe program and SkimSrv2 directory

   9. Configure each for separate band segments, below is an example that I use
   NOTE: each .ini file has the rate set to 192k (Rate=2) which selects SegmentSel192

SkimSrv (one)

[Skimmer]
CenterFreqs48=136750,475000,1821250,1863750,3521250,3563750,5353250,5395750,7021250,7063750,10121750,14021250,14063750,18089250,21021250,21063750,24911250,28021250,28063750,50021250,50063750,50106250,50148750,50191250
CenterFreqs96=136750,475000,1842500,3542500,5374500,7042500,10142500,14042500,18110500,21042500,24932500,28042500,28127500,28212500,50042500,50127500,50212500
CenterFreqs192=136750,475000,1885000,3585000,5417000,7085000,10185000,14085000,18153000,21085000,24975000,28085000,28255000,50085000,50255000
SegmentSel48=111111110000000000000000
SegmentSel96=11111111000000000
SegmentSel192=111111110000000
CwSegments=135700-137800,472000-479000,1800000-2000000,3500000-3600000,5330000-5407000,7000000-7125000,10100000-10130000,14000000-14150000,18068000-18115000,21000000-21200000,24890000-24935000,28000000-28300000,50000000-50200000

SkimSrv2 (two)

[Skimmer]
CenterFreqs48=136750,475000,1821250,1863750,3521250,3563750,5353250,5395750,7021250,7063750,10121750,14021250,14063750,18089250,21021250,21063750,24911250,28021250,28063750,50021250,50063750,50106250,50148750,50191250
CenterFreqs96=136750,475000,1842500,3542500,5374500,7042500,10142500,14042500,18110500,21042500,24932500,28042500,28127500,28212500,50042500,50127500,50212500
CenterFreqs192=136750,475000,1885000,3585000,5417000,7085000,10185000,14085000,18153000,21085000,24975000,28085000,28255000,50085000,50255000
SegmentSel48=000000001111111000000000
SegmentSel96=00000000111111110
SegmentSel192=000000001111111
CwSegments=135700-137800,472000-479000,1800000-2000000,3500000-3600000,5330000-5407000,7000000-7125000,10100000-10130000,14000000-14150000,18068000-18115000,21000000-21200000,24890000-24935000,28000000-28300000,50000000-50200000


   10. Start the first *SkimSrv* instance of CW Skimmer Server, Make sure it comes up working
   then start the second *SkimSrv2* and verify that it also starts skimming. If you get errors
   check the CWSL_Tee.log and HermesIntf_log_file.txt in each's working directory.

NOTE: Step 5 is critical, otherwise the two instances will both try to write to
the same file (*%appdata%\Afreet\Products\SkimSrv\Spots.txt*), which isn't
supported, and you'll get an error message similar to "Could not open
Spots.txt."  Renaming the executable causes the second instance to write
to %appdata%\Afreet\Products\*SkimSrv2*\Spots.txt instead.

Also critical is making sure they don't use the same Telnet port (7300).


   11. Now you hopefully have 2 SkimSrv's running skimming 15 bands, here's how to add
   RttySkimSrv on top of that. Configure your RttySkimSrv.ini, NOTE: change the telnet
   port to something other that the two above, say 7304

   here's mine configured for 6 of the 192k bands:

[Skimmer]
Contest=DX
CenterFreqs48=136750,475000,1821250,1863750,3521250,3563750,5353250,5395750,7021250,7063750,10121750,14021250,14063750,18089250,21021250,21063750,24911250,28021250,28063750,50021250,50063750,50106250,50148750,50191250
CenterFreqs96=136750,475000,1842500,3542500,5374500,7042500,10142500,14042500,18110500,21042500,24932500,28042500,28127500,28212500,50042500,50127500,50212500
CenterFreqs192=136750,475000,1885000,3585000,5417000,7085000,10185000,14085000,18153000,21085000,24975000,28085000,28255000,50085000,50255000
SegmentSel48=111111110000000000000000
SegmentSel96=00111110000000000
SegmentSel192=001111110000000
RttySegments=135700-137800,472000-479000,1800000-2000000,3500000-3600000,5330000-5407000,7000000-7125000,10100000-10130000,14000000-14150000,18068000-18115000,21000000-21200000,24890000-24935000,28000000-28300000,50000000-50200000

The Author of CWSL_Tee, https://github.com/HrochL/CWSL, has other apps that can work with
this setup. I have only tried one, HDSDR with his Extio_CWSL.dll plugin. Although I beleive
all the other apps should work a well since they can go up to a max of 32 bands.

With the 3 Skimmers running as described above, here's how to use HDSDR on top of that.


   12. Install HDSDR (http://www.hdsdr.de/) and copy the Extio_CWSL.dll plugin to it's directory.
   You need to have at least one Skimmer running with the CWSL_Tee.dll


   13. Start HDSDR and select the Ext plugin Extio_CWSL.dll. You should see a small selection box
   pop up on the upper right of the screen. Here you can select any of those 15 bands. Then start
   HDSDR. Note you will not be able to control the LO frequency, but can tune or click around the
   bandscope.


   14. To take advantage of the ANAN-200D's two ADC inputs, say you want to use two different
   antennas, you need to add a line in CWSL_Tee.cfg. Here's an example of mine:

HermesIntf
63
0000000000000100

   NOTE receiver1 is the left most bit and receiver16 is the right most. I chose this to match
   the way the SkimSrv.ini files order the receivers. '0' will select ADC1 and '1' will select ADC2.
   So above all ADC's are set to ADC1 except receiver14 which will use ADC2 input.

   The way I see this feature useful is to arrange your SkimSrv.ini files to double up on
   the bands you want to use different anntennas. Here's an example:

[Skimmer]
CenterFreqs48=136750,475000,1821250,1863750,3521250,3563750,5353250,5395750,7021250,7063750,10121750,14021250,14063750,18089250,21021250,21063750,24911250,28021250,28063750,50021250,50063750,50106250,50148750,50191250
CenterFreqs96=136750,475000,1842500,3542500,5374500,7042500,10142500,14042500,18110500,21042500,24932500,28042500,28127500,28212500,50042500,50127500,50212500
CenterFreqs192=1885000,1885000,3585000,3585000,7085000,7085000,14085000,14085000,18153000,21085000,24975000,28085000,28255000,50085000,50255000
SegmentSel48=000000001111111000000000
SegmentSel96=00000000111111110
SegmentSel192=000000001111111
CwSegments=135700-137800,472000-479000,1800000-2000000,3500000-3600000,5330000-5407000,7000000-7125000,10100000-10130000,14000000-14150000,18068000-18115000,21000000-21200000,24890000-24935000,28000000-28300000,50000000-50200000


   Notice I've doubled up the 160m, 80m, 40m, and 20m bands, replacing the previous since the count needs
   to stay the same. Now edit the CWSL_Tee.cfg and add a line for the ADC's like:

0101010100000000

   You'll notice that SkimSrv is fine with this arrangement.



