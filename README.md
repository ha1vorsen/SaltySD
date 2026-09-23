## SaltySD

SaltySD is a collection of modifications for 3DS ROMs which allows them to load various resources from the SD card rather than from romFS. This allows for easier modability and testing in some games which use archives to hold a majority of their files.

**Currently Supported Games**

 * Smash 3DS (SaltySD v2.0)
 * Pokemon Sun and Moon (SaltySD v1.2)
 * Pokemon Ultra Sun and Ultra Moon (SaltySD v1.0)

Files are redirected into their own directory under sdmc:/saltysd/ based on the subdirectory name in the respository (i.e. Pokemon Sun and Moon load their override files from sdmc:/saltysd/SunMoon/). Smash is a unique case here, so for further information, patching instructions can be found in the PATCHING.md files in each subdirectory.

