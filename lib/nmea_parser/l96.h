#define L96_SEARCH_GLONASS "$PMTK353,0,1,0,0,0*2A\r\n"             // Search GLONASS satellites only
#define L96_SEARCH_GPS "$PMTK353,1,0,0,0,0*2A\r\n"                 // Search GPS satellites only
#define L96_SEARCH_GPS_GLONASS "$PMTK353,1,1,0,0,0*2B\r\n"         // Search GPS and GLONASS satellites
#define L96_SEARCH_GPS_GLONASS_GALILEO "$PMTK353,1,1,1,0,0*2A\r\n" // Search GPS, GLONASS, Galileo satellites

#define L96_ENTER_FULL_ON "$PMTK225,0*2B\r\n"

#define L96_ENTER_STANDBY "$PMTK161,0*28\r\n"

#define L96_ENTER_ALLWAYS_LOCATE "$PMTK225,8*23\r\n"
#define L96_REPLY_ALLWAYS_LOCATE "$PMTK001,225,3*35\r\n"

#define L96_ENTER_GLP "$PQGLP,W,1,1*21\r\n"
#define L96_REPLY_GLP "$PQGLP,W,OK*09\r\n"

#define L96_EXIT_GLP "$PQGLP,W,0,1*20\r\n"
#define L96_REPLY_GLP "$PQGLP,W,OK*09\r\n"

#define L96_AIC_ENABLE "$PMTK 286,1*23\r\n"
#define L96_AIC_DISABLE "$PMTK 286,0*22\r\n"
