/*
  TilerDX, a demo program to make tiles with the Direct2D backend.
  Copyright (C) 2017 Script&Go

  Nope, no licence.
*/

#include <iostream>
#include <iomanip>
#include <limits>
#include <locale>
#include <codecvt>

#include <osmscout/Database.h>
#include <osmscout/MapService.h>

#include <osmscout/MapPainterDirectX.h>

#include <osmscout/util/StopClock.h>
#include <osmscout/util/Tiling.h>

#include <d2d1.h>
#include <d2d1helper.h>

/*
  Example for the nordrhein-westfalen.osm (to be executed in the Demos top
  level directory), drawing the "Ruhrgebiet":

  src/Tiler ../maps/nordrhein-westfalen ../stylesheets/standard.oss 51.2 6.5 51.7 8 10 13
*/

template<class Interface>
inline void SafeRelease(
	Interface **ppInterfaceToRelease
	)
{
	if (*ppInterfaceToRelease != NULL)
	{
		(*ppInterfaceToRelease)->Release();

		(*ppInterfaceToRelease) = NULL;
	}
}


static const unsigned int tileWidth = 256;
static const unsigned int tileHeight = 256;
static const double       DPI = 96.0;
static const int          tileRingSize = 1;

bool write_png(IWICBitmap *bmp, IWICImagingFactory *factory, const wchar_t* file_name)
{
	IWICStream *stream;
	auto err = factory->CreateStream(&stream);
	if (err != S_OK)
		throw err;

	err = stream->InitializeFromFilename(file_name, GENERIC_WRITE);
	if (err != S_OK)
		throw err;

	IWICBitmapEncoder *encoder;
	err = factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder);
	if (err != S_OK)
		throw err;
	err = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	if (err != S_OK)
		throw err;

	IWICBitmapFrameEncode *frameEncode;
	err = encoder->CreateNewFrame(&frameEncode, NULL);
	unsigned width, height;
	WICPixelFormatGUID format;

	bmp->GetPixelFormat(&format);
	bmp->GetSize(&width, &height);
	frameEncode->Initialize(NULL);
	frameEncode->SetSize(width, height);
	frameEncode->SetPixelFormat(&format);
	err = frameEncode->WriteSource(bmp, NULL);
	if (err != S_OK)
		throw err;
	err = frameEncode->Commit();
	if (err != S_OK)
		throw err;
	encoder->Commit();

	SafeRelease(&frameEncode);
	SafeRelease(&stream);
	SafeRelease(&encoder);
	return true;
}

void MergeTilesToMapData(const std::list<osmscout::TileRef>& centerTiles,
	const osmscout::MapService::TypeDefinition& ringTypeDefinition,
	const std::list<osmscout::TileRef>& ringTiles,
	osmscout::MapData& data)
{
	std::unordered_map<osmscout::FileOffset, osmscout::NodeRef> nodeMap(10000);
	std::unordered_map<osmscout::FileOffset, osmscout::WayRef>  wayMap(10000);
	std::unordered_map<osmscout::FileOffset, osmscout::AreaRef> areaMap(10000);
	std::unordered_map<osmscout::FileOffset, osmscout::WayRef>  optimizedWayMap(10000);
	std::unordered_map<osmscout::FileOffset, osmscout::AreaRef> optimizedAreaMap(10000);

	osmscout::StopClock uniqueTime;

	for (auto tile : centerTiles) {
		tile->GetNodeData().CopyData([&nodeMap](const osmscout::NodeRef& node) {
			nodeMap[node->GetFileOffset()] = node;
		});

		//---

		tile->GetOptimizedWayData().CopyData([&optimizedWayMap](const osmscout::WayRef& way) {
			optimizedWayMap[way->GetFileOffset()] = way;
		});

		tile->GetWayData().CopyData([&wayMap](const osmscout::WayRef& way) {
			wayMap[way->GetFileOffset()] = way;
		});

		//---

		tile->GetOptimizedAreaData().CopyData([&optimizedAreaMap](const osmscout::AreaRef& area) {
			optimizedAreaMap[area->GetFileOffset()] = area;
		});

		tile->GetAreaData().CopyData([&areaMap](const osmscout::AreaRef& area) {
			areaMap[area->GetFileOffset()] = area;
		});
	}

	for (auto tile : ringTiles) {
		tile->GetNodeData().CopyData([&ringTypeDefinition, &nodeMap](const osmscout::NodeRef& node) {
			if (ringTypeDefinition.nodeTypes.IsSet(node->GetType())) {
				nodeMap[node->GetFileOffset()] = node;
			}
		});

		//---

		tile->GetOptimizedWayData().CopyData([&ringTypeDefinition, &optimizedWayMap](const osmscout::WayRef& way) {
			if (ringTypeDefinition.optimizedWayTypes.IsSet(way->GetType())) {
				optimizedWayMap[way->GetFileOffset()] = way;
			}
		});

		tile->GetWayData().CopyData([&ringTypeDefinition, &wayMap](const osmscout::WayRef& way) {
			if (ringTypeDefinition.wayTypes.IsSet(way->GetType())) {
				wayMap[way->GetFileOffset()] = way;
			}
		});

		//---

		tile->GetOptimizedAreaData().CopyData([&ringTypeDefinition, &optimizedAreaMap](const osmscout::AreaRef& area) {
			if (ringTypeDefinition.optimizedAreaTypes.IsSet(area->GetType())) {
				optimizedAreaMap[area->GetFileOffset()] = area;
			}
		});

		tile->GetAreaData().CopyData([&ringTypeDefinition, &areaMap](const osmscout::AreaRef& area) {
			if (ringTypeDefinition.areaTypes.IsSet(area->GetType())) {
				areaMap[area->GetFileOffset()] = area;
			}
		});
	}

	uniqueTime.Stop();

	//std::cout << "Make data unique time: " << uniqueTime.ResultString() << std::endl;

	osmscout::StopClock copyTime;

	data.nodes.reserve(nodeMap.size());
	data.ways.reserve(wayMap.size() + optimizedWayMap.size());
	data.areas.reserve(areaMap.size() + optimizedAreaMap.size());

	for (const auto& nodeEntry : nodeMap) {
		data.nodes.push_back(nodeEntry.second);
	}

	for (const auto& wayEntry : wayMap) {
		data.ways.push_back(wayEntry.second);
	}

	for (const auto& wayEntry : optimizedWayMap) {
		data.ways.push_back(wayEntry.second);
	}

	for (const auto& areaEntry : areaMap) {
		data.areas.push_back(areaEntry.second);
	}

	for (const auto& areaEntry : optimizedAreaMap) {
		data.areas.push_back(areaEntry.second);
	}

	copyTime.Stop();

	if (copyTime.GetMilliseconds() > 20) {
		osmscout::log.Warn() << "Copying data from tile to MapData took " << copyTime.ResultString();
	}
}

int main(int argc, char* argv[])
{
	std::string  map;
	std::string  style;
	double       latTop, latBottom, lonLeft, lonRight;
	unsigned int startLevel;
	unsigned int endLevel;

	if (argc != 9) {
		std::cerr << "DrawMap ";
		std::cerr << "<map directory> <style-file> ";
		std::cerr << "<lat_top> <lon_left> <lat_bottom> <lon_right> ";
		std::cerr << "<start_zoom>" << std::endl;
		std::cerr << "<end_zoom>" << std::endl;
		return 1;
	}

	map = argv[1];
	style = argv[2];

	if (sscanf(argv[3], "%lf", &latTop) != 1) {
		std::cerr << "lon is not numeric!" << std::endl;
		return 1;
	}

	if (sscanf(argv[4], "%lf", &lonLeft) != 1) {
		std::cerr << "lat is not numeric!" << std::endl;
		return 1;
	}

	if (sscanf(argv[5], "%lf", &latBottom) != 1) {
		std::cerr << "lon is not numeric!" << std::endl;
		return 1;
	}

	if (sscanf(argv[6], "%lf", &lonRight) != 1) {
		std::cerr << "lat is not numeric!" << std::endl;
		return 1;
	}

	if (sscanf(argv[7], "%u", &startLevel) != 1) {
		std::cerr << "start zoom is not numeric!" << std::endl;
		return 1;
	}

	if (sscanf(argv[8], "%u", &endLevel) != 1) {
		std::cerr << "end zoom is not numeric!" << std::endl;
		return 1;
	}

	osmscout::DatabaseParameter databaseParameter;
	osmscout::DatabaseRef       database = std::make_shared<osmscout::Database>(databaseParameter);
	osmscout::MapServiceRef     mapService = std::make_shared<osmscout::MapService>(database);

	if (!database->Open(map.c_str())) {
		std::cerr << "Cannot open database" << std::endl;

		return 1;
	}

	osmscout::StyleConfigRef styleConfig = std::make_shared<osmscout::StyleConfig>(database->GetTypeConfig());

	if (!styleConfig->Load(style)) {
		std::cerr << "Cannot open style" << std::endl;
	}

	osmscout::TileProjection      projection;
	osmscout::MapParameter        drawParameter;
	osmscout::AreaSearchParameter searchParameter;

	// Change this, to match your system
	//drawParameter.SetFontName("/usr/share/fonts/truetype/msttcorefonts/Verdana.ttf");
	drawParameter.SetFontName("Verdana");
	drawParameter.SetFontSize(2.0);
	// Fadings make problems with tile approach, we disable it
	drawParameter.SetDrawFadings(false);
	// To get accurate label drawing at tile borders, we take into account labels
	// of other than the current tile, too.
	drawParameter.SetDropNotVisiblePointLabels(false);

	drawParameter.SetRenderSeaLand(true);

	searchParameter.SetUseLowZoomOptimization(true);
	searchParameter.SetMaximumAreaLevel(3);

	ID2D1Factory *directXFactory;
	auto err = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &directXFactory);
	if (err != S_OK)
		throw err;
	IDWriteFactory *writeFactory;
	err = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(writeFactory), reinterpret_cast<IUnknown **>(&writeFactory));
	if (err != S_OK)
		throw err;

	// TODO schopin: proper error checking.
	IWICImagingFactory *wicFactory;

	err = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (err != S_OK)
		throw err;
	err = CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&wicFactory
	);
	if (err != S_OK)
		throw err;

	IWICBitmap *bmp;
	err = wicFactory->CreateBitmap(
		tileWidth, tileHeight,
		GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
		&bmp);
	if (err != S_OK)
		throw err;

	ID2D1RenderTarget *renderTarget;
	D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
	err = directXFactory->CreateWicBitmapRenderTarget(bmp, props, &renderTarget);
	if (err != S_OK)
		throw err;

	osmscout::MapPainterDirectX painter(styleConfig, directXFactory, writeFactory);

	for (size_t level = std::min(startLevel, endLevel);
		level <= std::max(startLevel, endLevel);
		level++) {
		osmscout::Magnification magnification;

		magnification.SetLevel(level);

		osmscout::OSMTileId     tileA(osmscout::GeoCoord(latBottom, lonLeft).GetOSMTile(magnification));
		osmscout::OSMTileId     tileB(osmscout::GeoCoord(latTop, lonRight).GetOSMTile(magnification));
		uint32_t                xTileStart = std::min(tileA.GetX(), tileB.GetX());
		uint32_t                xTileEnd = std::max(tileA.GetX(), tileB.GetX());
		uint32_t                xTileCount = xTileEnd - xTileStart + 1;
		uint32_t                yTileStart = std::min(tileA.GetY(), tileB.GetY());
		uint32_t                yTileEnd = std::max(tileA.GetY(), tileB.GetY());
		uint32_t                yTileCount = yTileEnd - yTileStart + 1;

		std::cout << "Drawing zoom " << level << ", " << (xTileCount)*(yTileCount) << " tiles [" << xTileStart << "," << yTileStart << " - " << xTileEnd << "," << yTileEnd << "]" << std::endl;

		double minTime = std::numeric_limits<double>::max();
		double maxTime = 0.0;
		double totalTime = 0.0;

		osmscout::MapService::TypeDefinition typeDefinition;

		for (auto type : database->GetTypeConfig()->GetTypes()) {
			bool hasLabel = false;

			if (type->CanBeNode()) {
				if (styleConfig->HasNodeTextStyles(type,
					magnification)) {
					typeDefinition.nodeTypes.Set(type);
					hasLabel = true;
				}
			}

			if (type->CanBeArea()) {
				if (styleConfig->HasAreaTextStyles(type,
					magnification)) {
					if (type->GetOptimizeLowZoom() && searchParameter.GetUseLowZoomOptimization()) {
						typeDefinition.optimizedAreaTypes.Set(type);
					}
					else {
						typeDefinition.areaTypes.Set(type);
					}

					hasLabel = true;
				}
			}

			if (hasLabel) {
				std::cout << "TYPE " << type->GetName() << " might have labels" << std::endl;
			}
		}

		for (uint32_t y = yTileStart; y <= yTileEnd; y++) {
			for (uint32_t x = xTileStart; x <= xTileEnd; x++) {
				osmscout::StopClock timer;
				osmscout::GeoBox    boundingBox;
				osmscout::MapData   data;
				/*
			agg::rendering_buffer rbuf(buffer,
									   tileWidth*xTileCount,
									   tileHeight*yTileCount,
									   tileWidth*xTileCount*3);
									   */

				projection.Set(osmscout::OSMTileId(x, y),
					magnification,
					DPI,
					tileWidth,
					tileHeight);

				projection.GetDimensions(boundingBox);

				std::cout << "Drawing tile " << level << "." << y << "." << x << " " << boundingBox.GetDisplayText() << std::endl;


				std::list<osmscout::TileRef> centerTiles;

				mapService->LookupTiles(magnification,
					boundingBox,
					centerTiles);

				mapService->LoadMissingTileData(searchParameter,
					*styleConfig,
					centerTiles);

				std::map<osmscout::TileId, osmscout::TileRef> ringTileMap;

				for (uint32_t ringY = y - tileRingSize; ringY <= y + tileRingSize; ringY++) {
					for (uint32_t ringX = x - tileRingSize; ringX <= x + tileRingSize; ringX++) {
						if (ringX == x && ringY == y) {
							continue;
						}

						osmscout::GeoBox boundingBox(osmscout::OSMTileId(ringX, ringY).GetBoundingBox(magnification));


						std::list<osmscout::TileRef> tiles;

						mapService->LookupTiles(magnification,
							boundingBox,
							tiles);

						for (const auto& tile : tiles) {
							ringTileMap[tile->GetId()] = tile;
						}
					}
				}

				std::list<osmscout::TileRef> ringTiles;

				for (const auto& tileEntry : ringTileMap) {
					ringTiles.push_back(tileEntry.second);
				}

				mapService->LoadMissingTileData(searchParameter,
					magnification,
					typeDefinition,
					ringTiles);

				MergeTilesToMapData(centerTiles,
					typeDefinition,
					ringTiles,
					data);
				mapService->GetGroundTiles(projection, data.groundTiles);

				renderTarget->BeginDraw();
				painter.DrawMap(projection,
					drawParameter,
					data,
					renderTarget);
				err = renderTarget->EndDraw();
				if (err != S_OK)
					throw err;

				timer.Stop();

				double time = timer.GetMilliseconds();

				minTime = std::min(minTime, time);
				maxTime = std::max(maxTime, time);
				totalTime += time;

				std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
				std::wstring output = converter.from_bytes(osmscout::NumberToString(level)) + L"_" + converter.from_bytes(osmscout::NumberToString(x)) + L"_" + converter.from_bytes(osmscout::NumberToString(y)) + L".png";

				write_png(bmp, wicFactory, output.c_str());
			}
		}

		/*
		rbuf.attach(buffer,
			tileWidth*xTileCount,
			tileHeight*yTileCount,
			tileWidth*xTileCount * 3);

		std::string output = osmscout::NumberToString(level) + "_full_map.ppm";

		write_ppm(rbuf, output.c_str());
		*/

		std::cout << "=> Time: ";
		std::cout << "total: " << totalTime << " msec ";
		std::cout << "min: " << minTime << " msec ";
		std::cout << "avg: " << totalTime / (xTileCount*yTileCount) << " msec ";
		std::cout << "max: " << maxTime << " msec" << std::endl;
	}

	SafeRelease(&renderTarget);
	SafeRelease(&bmp);
	SafeRelease(&wicFactory);
	SafeRelease(&writeFactory);
	SafeRelease(&directXFactory);


	database->Close();

	return 0;
}
