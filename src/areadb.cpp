#include "areadb.h"
#include <string>

AreaDB gAreaDB;
MapDB gMapDB;
LoadingScreensDB gLoadingScreensDB;
LightDB gLightDB;
LightSkyboxDB gLightSkyboxDB;
LightIntBandDB gLightIntBandDB;
LightFloatBandDB gLightFloatBandDB;
GroundEffectDoodadDB gGroundEffectDoodadDB;
GroundEffectTextureDB gGroundEffectTextureDB;
LiquidTypeDB gLiquidTypeDB;

void OpenDBs()
{
	gAreaDB.open();
	gMapDB.open();
	gLoadingScreensDB.open();
	gLightDB.open();
	gLightSkyboxDB.open();
	gLightIntBandDB.open();
	gLightFloatBandDB.open();
	gGroundEffectDoodadDB.open();
	gGroundEffectTextureDB.open();
	gLiquidTypeDB.open();
}


std::string AreaDB::getAreaName( unsigned int pAreaID )
{
	unsigned int regionID = 0;
	std::string areaName = "";
	try 
	{
		AreaDB::Record rec = gAreaDB.getByID( pAreaID );
		areaName = rec.getLocalizedString( AreaDB::Name );
		regionID = rec.getUInt( AreaDB::Region );
	} 
	catch(AreaDB::NotFound)
	{
		areaName = "Unknown location";
	}
	if (regionID != 0) 
	{
		try 
		{
			AreaDB::Record rec = gAreaDB.getByID( regionID );
			areaName = std::string(rec.getLocalizedString( AreaDB::Name )) + std::string(": ") + areaName;
		} 
		catch(AreaDB::NotFound)
		{
			areaName = "Unknown location";
		}
	}
	return areaName;
}
