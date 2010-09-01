#include "AuctionHouseBot.h"
#include "ProgressBar.h"
#include "ObjectMgr.h"
#include "AuctionHouseMgr.h"

#include "Policies/SingletonImp.h"

INSTANTIATE_SINGLETON_1( AuctionHouseBot );

using namespace std;

AuctionHouseBot::AuctionHouseBot()
{
    AllianceConfig = AHBConfig(2);
    HordeConfig = AHBConfig(6);
    NeutralConfig = AHBConfig(7);
    m_FakeGuid.Set(std::numeric_limits< uint32 >::max());
    ItemPool.resize(AHB_QUALITY_MAX);
    for (uint32 j=0; j<AHB_QUALITY_MAX; ++j)
        for (uint32 i=0;i<MAX_ITEM_CLASS;++i) 
            ItemPool[j].resize(MAX_ITEM_CLASS);
}

AuctionHouseBot::~AuctionHouseBot()
{

}

//Set statisque of items on one faction.
uint32 AuctionHouseBot::SetStat(AHBConfig& config)
{
    struct s_itemInAh
    {
        uint32 count;
        std::vector<uint32> itemClass;
        s_itemInAh() { count = 0; itemClass.resize(MAX_ITEM_CLASS); }
    };


    std::vector<std::vector<uint32>> ItemsInAH(AHB_QUALITY_MAX, vector<uint32> (MAX_ITEM_CLASS));

    AuctionHouseEntry const* ahEntry = sAuctionMgr.GetAuctionHouseEntryByFaction(config.GetAHFID());
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = auctionHouse->GetAuctionsBegin();itr != auctionHouse->GetAuctionsEnd();++itr)
    {
        AuctionEntry *Aentry = itr->second;
        Item *item = sAuctionMgr.GetAItem(Aentry->item_guidlow);
        if (item)
        {
            ItemPrototype const *prototype = item->GetProto();
            if (prototype)
            {
                if ( Aentry->owner == GetAHBObjectGuid().GetRawValue())
                {
                    ++ItemsInAH[prototype->Quality][prototype->Class];
                }
            }
        }
    }
    uint32 count=0;
    for (uint32 j=0; j<AHB_QUALITY_MAX; ++j)
    {
        for (uint32 i=0;i<MAX_ITEM_CLASS;++i) 
        {
            config.ItemInfos[j].ItemClassInfos[i].SetMissItems(ItemsInAH[j][i]);
            count+=config.ItemInfos[j].ItemClassInfos[i].GetMissItems();
        }
    }

    if (debug_Out)
    {
        sLog.outString("Missed Item\tGrey\tWhite\tGreen\tBlue\tPurple\tOrange\tYellow");
        for (uint32 i=0; i<MAX_ITEM_CLASS;++i)
        {
            sLog.outString("%-11s\t%u\t%u\t%u\t%u\t%u\t%u\t%u",config.ItemInfos[0].ItemClassInfos[i].GetName().c_str(),config.ItemInfos[0].ItemClassInfos[i].GetMissItems(),config.ItemInfos[1].ItemClassInfos[i].GetMissItems(),config.ItemInfos[2].ItemClassInfos[i].GetMissItems(),
                config.ItemInfos[3].ItemClassInfos[i].GetMissItems(),config.ItemInfos[4].ItemClassInfos[i].GetMissItems(),config.ItemInfos[5].ItemClassInfos[i].GetMissItems(),config.ItemInfos[6].ItemClassInfos[i].GetMissItems());
        }
    }

    return count;
}

bool AuctionHouseBot::getRandomArray( AHBConfig& config, std::vector<s_randomArray>& ra, const std::vector<std::vector<uint32>>& addedItem  )
{
    ra.clear();
    s_randomArray miss_item;
    bool Ok=false;

    for (uint32 j=0; j<AHB_QUALITY_MAX; ++j)
    {
        for (uint32 i=0; i<MAX_ITEM_CLASS; ++i)
            if ((config.ItemInfos[j].ItemClassInfos[i].GetMissItems()   > addedItem[j][i]) && ItemPool[j][i].size() >0)    
            {
                miss_item.color=j;
                miss_item.itemclass=i;
                ra.push_back(miss_item);
                Ok=true;
            }
    }

    return Ok;
}


void AuctionHouseBot::SetPricesOfItem(const Item *item,AHBConfig& config, uint32& buyp, uint32& bidp, uint32& stackcnt, e_ahb_quality AHB_ITEMS)
{
    if (config.ItemInfos[AHB_ITEMS].GetMaxStack() != 0)
    {
        stackcnt = urand(1, minValue(item->GetMaxStackCount(), config.ItemInfos[AHB_ITEMS].GetMaxStack()));
    }
    buyp *= urand(config.ItemInfos[AHB_ITEMS].GetMinPrice(), config.ItemInfos[AHB_ITEMS].GetMaxPrice()) * stackcnt;
    buyp /= 100;
    bidp = buyp * urand(config.ItemInfos[AHB_ITEMS].GetMinBidPrice(), config.ItemInfos[AHB_ITEMS].GetMaxBidPrice());
    bidp /= 100;
}

void AuctionHouseBot::addNewAuctions(AHBConfig& config)
{
    uint32 MissItems=SetStat(config);
    if (MissItems==0) return;

    uint32 items;
    if (MissItems > ItemsPerCycleBoost) 
    {
        items=ItemsPerCycleBoost;
        sLog.outString("AHBot> Boost value used to fill AH! (if this happens often adjust both ItemsPerCycle in mangosd.conf)");
    }
    else items=ItemsPerCycleNormal;

    AuctionHouseEntry const* ahEntry = sAuctionMgr.GetAuctionHouseEntryByFaction(config.GetAHFID());
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);

    std::vector<s_randomArray> RandArray;
    std::vector<std::vector<uint32>> ItemsAdded(AHB_QUALITY_MAX,std::vector<uint32> (MAX_ITEM_CLASS));
    // only insert a few at a time, so as not to peg the processor
    while (getRandomArray(config,RandArray, ItemsAdded) && (items>0))
    {
        --items;
        uint32 itemID = 0;
        uint32 pos =  (urand(0,RandArray.size()-1));

        itemID = ItemPool[RandArray[pos].color][RandArray[pos].itemclass][urand(0,ItemPool[RandArray[pos].color][RandArray[pos].itemclass].size()-1)];
        ++ ItemsAdded[RandArray[pos].color][RandArray[pos].itemclass];

        if (itemID == 0)
        {
            if (debug_Out)
                sLog.outString("AHBot> Item::CreateItem() - Unable to find item");
            continue;
        }

        ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(itemID);
        if (prototype == NULL)
        {
            sLog.outString("AHBot> Huh?!?! prototype == NULL");
            continue;
        }

        Item* item = Item::CreateItem(itemID, 1);
        if (item == NULL)
        {
            sLog.outString("AHBot> Item::CreateItem() returned NULL");
            return;
        }

        uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(itemID);
        if (randomPropertyId != 0)
            item->SetItemRandomPropertiesNoUpdate(randomPropertyId);

        uint32 buyoutPrice;
        uint32 bidPrice = 0;
        uint32 stackCount = urand(1, item->GetMaxStackCount());
        if(sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYPRICE_SELLER))
            buyoutPrice  = prototype->BuyPrice * item->GetCount();
        else
            buyoutPrice  = prototype->SellPrice * item->GetCount();
        SetPricesOfItem(item, config, buyoutPrice, bidPrice, stackCount, ((e_ahb_quality) prototype->Quality));
        item->SetCount(stackCount);
        item->SetOwnerGUID(GetAHBObjectGuid().GetRawValue());
        AuctionEntry* auctionEntry = new AuctionEntry;
        auctionEntry->Id = sObjectMgr.GenerateAuctionID();
        auctionEntry->item_guidlow = item->GetGUIDLow();
        auctionEntry->item_template = item->GetEntry();
        auctionEntry->owner =((uint32) GetAHBObjectGuid().GetRawValue());
        auctionEntry->startbid = bidPrice;
        auctionEntry->buyout = buyoutPrice;
        auctionEntry->bidder = 0;
        auctionEntry->bid = 0;
        auctionEntry->deposit = 0;
        auctionEntry->expire_time = (time_t) (urand(config.GetMinTime(), config.GetMaxTime()) * 60 * 60 + time(NULL));
        auctionEntry->auctionHouseEntry = ahEntry;
        item->SaveToDB();
        sAuctionMgr.AddAItem(item);
        auctionHouse->AddAuction(auctionEntry);
        auctionEntry->SaveToDB();
    }
}

void AuctionHouseBot::addNewAuctionBuyerBotBid(AHBConfig *config, WorldSession *session)
{
    // Fetches content of selected AH
    AuctionHouseEntry const* ahEntry = sAuctionMgr.GetAuctionHouseEntryByFaction(config->GetAHFID());
    AuctionHouseObject* auctionHouse = sAuctionMgr.GetAuctionsMap(ahEntry);
    vector<uint32> possibleBids;

    for (AuctionHouseObject::AuctionEntryMap::const_iterator itr = auctionHouse->GetAuctionsBegin();itr != auctionHouse->GetAuctionsEnd();++itr)
    {
        // Check if the auction is ours
        // if it is, we skip this iteration.
        if (itr->second->owner == std::numeric_limits< int >::max())
        {
            continue;
        }
        // Check that we haven't bidded in this auction already.
        if (itr->second->bidder != std::numeric_limits< int >::max())
        {
            uint32 tmpdata = itr->second->Id;
            possibleBids.push_back(tmpdata);
        }
    }

    for (uint32 count = 0;count < config->GetBidsPerInterval();++count)
    {

        // Do we have anything to bid? If not, stop here.
        if (possibleBids.empty())
        {
            count = config->GetBidsPerInterval();
            continue;
        }

        // Choose random auction from possible auctions
        uint32 vectorPos = urand(0, possibleBids.size() - 1);
        uint32 auctionID = possibleBids[vectorPos];

        // Erase the auction from the vector to prevent bidding on item in next iteration.
        vector<uint32>::iterator iter = possibleBids.begin();
        advance(iter, vectorPos);
        possibleBids.erase(iter);

        // from auctionhousehandler.cpp, creates auction pointer & player pointer
        AuctionEntry* auction = auctionHouse->GetAuction(auctionID);
        if (!auction)
        {
            sLog.outError("Item doesn't exists, perhaps bought already?");
            continue;
        }

        // get exact item information
        Item *pItem = sAuctionMgr.GetAItem(auction->item_guidlow);
        if (!pItem)
        {
            sLog.outError("Item doesn't exists, perhaps bought already?");
            continue;
        }

        // get item prototype
        ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(auction->item_template);

        // check which price we have to use, startbid or if it is bidded already
        if (debug_Out)
        {
            sLog.outError("Auction Number: %u", auction->Id);
            sLog.outError("Item Template: %u", auction->item_template);
            sLog.outError("Buy Price: %u", prototype->BuyPrice);
            sLog.outError("Sell Price: %u", prototype->SellPrice);
            sLog.outError("Quality: %u", prototype->Quality);
        }
        uint32 currentprice;
        if (auction->bid)
        {
            currentprice = auction->bid;
            if (debug_Out)
                sLog.outError("Current Price: %u", auction->bid);
        }
        else
        {
            currentprice = auction->startbid;
            if (debug_Out)
                sLog.outError("Current Price: %u", auction->startbid);
        }
        uint32 bidprice;

        // Prepare portion from maximum bid
        uint32 tmprate2 = urand(0, 100);
        double tmprate = static_cast<double>(tmprate2);
        if (debug_Out)
            sLog.outError("tmprate: %f", tmprate);

        double bidrate = tmprate / 100;
        if (debug_Out)
            sLog.outError("bidrate: %f", bidrate);

        long double bidMax = 0;

        // check that bid has acceptable value and take bid based on vendorprice, stacksize and quality
        if(sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYPRICE_BUYER))
        {                
            if (currentprice < prototype->BuyPrice * pItem->GetCount() * config->ItemInfos[prototype->Quality].GetBuyerPrice())
            {
                bidMax = prototype->BuyPrice * pItem->GetCount() * config->ItemInfos[prototype->Quality].GetBuyerPrice();
            }
        }
        else
        {
            if (currentprice < prototype->SellPrice * pItem->GetCount() * config->ItemInfos[prototype->Quality].GetBuyerPrice())
            {
                bidMax = prototype->SellPrice * pItem->GetCount() * config->ItemInfos[prototype->Quality].GetBuyerPrice();
            }
        }

        if (debug_Out)
            sLog.outError("bidMax(succeed): %f", bidMax);

        // check some special items, and do recalculating to their prices
        switch (prototype->Class)
        {
            // ammo
        case 6:
            bidMax = 0;
            break;
        default:
            break;
        }

        if (bidMax == 0)
            continue;   // quality check failed to get bidmax, let's get out of here

        // Calculate our bid
        long double bidvalue = currentprice + ((bidMax - currentprice) * bidrate);
        if (debug_Out)
            sLog.outError("bidvalue: %f", bidvalue);

        // Convert to uint32
        bidprice = static_cast<uint32>(bidvalue);
        if (debug_Out)
            sLog.outError("bidprice: %u", bidprice);

        // Check our bid is high enough to be valid. If not, correct it to minimum.
        if ((currentprice + auction->GetAuctionOutBid()) > bidprice)
        {
            bidprice = currentprice + auction->GetAuctionOutBid();
            if (debug_Out)
                sLog.outError("bidprice(>): %u", bidprice);
        }

        // Check wether we do normal bid, or buyout
        if ((bidprice < auction->buyout) || (auction->buyout == 0))
        {

            if (auction->bidder > 0)
            {
                if (auction->bidder == GetAHBObjectGuid().GetRawValue())
                {
                    //pl->ModifyMoney(-int32(price - auction->bid));
                }
                else
                {
                    // mail to last bidder and return money
                    session->SendAuctionOutbiddedMail(auction , bidprice);
                    //pl->ModifyMoney(-int32(price));
                }
            }

            auction->bidder = GetAHBObjectGuid().GetRawValue();
            auction->bid = bidprice;

            // Saving auction into database
            CharacterDatabase.PExecute("UPDATE auction SET buyguid = '%u',lastbid = '%u' WHERE id = '%u'", auction->bidder, auction->bid, auction->Id);
        }
        else
        {
            //buyout
            if (GetAHBObjectGuid().GetRawValue() == auction->bidder)
            {
                //pl->ModifyMoney(-int32(auction->buyout - auction->bid));
            }
            else
            {
                //pl->ModifyMoney(-int32(auction->buyout));
                if (auction->bidder)
                {
                    session->SendAuctionOutbiddedMail(auction, auction->buyout);
                }
            }
            auction->bidder = GetAHBObjectGuid().GetRawValue();
            auction->bid = auction->buyout;

            // Send mails to buyer & seller
            sAuctionMgr.SendAuctionSuccessfulMail(auction);
            sAuctionMgr.SendAuctionWonMail(auction);

            // Remove item from auctionhouse
            sAuctionMgr.RemoveAItem(auction->item_guidlow);
            // Remove auction
            auctionHouse->RemoveAuction(auction->Id);
            // Remove from database
            auction->DeleteFromDB();
        }
    }
}

void AuctionHouseBot::Update()
{
    time_t _newrun = time(NULL);
    if ((sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO)==0) &&
        (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_ALLIANCE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_HORDE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_NEUTRAL_ENABLED)!=true))
         return;
    
    WorldSession _session(0, NULL, SEC_PLAYER, true, 0, LOCALE_enUS);

    // Add New Bids
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO)>0) addNewAuctions(AllianceConfig);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO)>0) addNewAuctions(HordeConfig);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO)>0) addNewAuctions(NeutralConfig);

    if (((_newrun - _lastrun_a) > (AllianceConfig.GetBiddingInterval() * 60)) && (AllianceConfig.GetBidsPerInterval() > 0) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_ALLIANCE_ENABLED)==true))
    {
        addNewAuctionBuyerBotBid(&AllianceConfig, &_session);
        _lastrun_a = _newrun;
    }

    if (((_newrun - _lastrun_h) > (HordeConfig.GetBiddingInterval() *60)) && (HordeConfig.GetBidsPerInterval() > 0) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_HORDE_ENABLED)==true))
    {
        addNewAuctionBuyerBotBid(&HordeConfig, &_session);
        _lastrun_h = _newrun;
    }

    if (((_newrun - _lastrun_n) > (NeutralConfig.GetBiddingInterval() * 60)) && (NeutralConfig.GetBidsPerInterval() > 0) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_NEUTRAL_ENABLED)==true))
    {
        addNewAuctionBuyerBotBid(&NeutralConfig, &_session);
        _lastrun_n = _newrun;
    }
}

void AuctionHouseBot::LoadConfig()
{
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO)>0)             LoadSellerValues(AllianceConfig);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO)>0)                LoadSellerValues(HordeConfig);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO)>0)              LoadSellerValues(NeutralConfig);
    if (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_ALLIANCE_ENABLED)==true)   LoadBuyerValues(AllianceConfig);
    if (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_HORDE_ENABLED)==true)      LoadBuyerValues(HordeConfig);
    if (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_NEUTRAL_ENABLED)==true)    LoadBuyerValues(NeutralConfig);
}

void AuctionHouseBot::Initialize()
{
    debug_Out = sConfig.GetIntDefault("AuctionHouseBot.DEBUG", 0);
    sLog.outString("");
    sLog.outString("-------------------------------");
    sLog.outString("------- AuctionHouseBot -------");
    sLog.outString("-------------------------------");
    sLog.outString("");
    sLog.outString("AHBot> New CORE by Cyberium (Original by Xeross, Naicisum, ChrisK, Paradox)");
    sLog.outString("AHBot> Includes AHBuyer by Kerbe and Paradox (Not tested)");

    if ((sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO)==0) &&
        (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_ALLIANCE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_HORDE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_NEUTRAL_ENABLED)!=true))
    {
        sLog.outString("All feature of AuctionHouseBot are disabled! (If you want to use it please set config in 'mangos.conf')");
        return;
    }
    if ((sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO)==0) && (sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO)==0))
    {
        sLog.outString("AuctionHouseBot SELLER is disabled! (If you want to use it please set config in 'mangos.conf')");
        return;
    }
    if ((sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_ALLIANCE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_HORDE_ENABLED)!=true) && (sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYER_NEUTRAL_ENABLED)!=true))
    {
        sLog.outString("AuctionHouseBot BUYER is disabled! (If you want to use it please set config in 'mangos.conf')");
    }

    ItemsPerCycleBoost = sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEMS_PER_CYCLE_BOOST);
    ItemsPerCycleNormal = sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEMS_PER_CYCLE_NORMAL);
    bool ItemAdded=false;
    QueryResult* results = (QueryResult*) NULL;
    char npcQuery[] = "SELECT distinct `item` FROM `npc_vendor`";
    sLog.outString(">> Loading npc...");
    results = WorldDatabase.PQuery(npcQuery);
    if (results != NULL)
    {
        barGoLink bar((int)results->GetRowCount());
        do
        {
            bar.step();
            Field* fields = results->Fetch();
            npcItems.push_back(fields[0].GetUInt32());

        } while (results->NextRow());

        delete results;
    }
    else
    {
        sLog.outString("AHBot> \"%s\" failed", npcQuery);
        return;
    }
    char lootQuery[] = "SELECT `item` FROM `creature_loot_template` UNION "
        "SELECT `item` FROM `disenchant_loot_template` UNION "
        "SELECT `item` FROM `fishing_loot_template` UNION "
        "SELECT `item` FROM `gameobject_loot_template` UNION "
        "SELECT `item` FROM `item_loot_template` UNION "
        "SELECT `item` FROM `milling_loot_template` UNION "
        "SELECT `item` FROM `pickpocketing_loot_template` UNION "
        "SELECT `item` FROM `prospecting_loot_template` UNION "
        "SELECT `item` FROM `skinning_loot_template`";

    sLog.outString(">> Loading Items...");
    results = WorldDatabase.PQuery(lootQuery);
    if (results != NULL)
    {

        barGoLink bar((int)results->GetRowCount());
        do
        {
            bar.step();
            Field* fields = results->Fetch();
            lootItems.push_back(fields[0].GetUInt32());
        } while (results->NextRow());
        delete results;
    }
    else
    {
        sLog.outString("AHBot> \"%s\" failed", lootQuery);
        return;
    }
    sLog.outString(">> Sorting and cleaning Items bases...");

    barGoLink bar(sItemStorage.MaxEntry);
    for (uint32 itemID = 0; itemID < sItemStorage.MaxEntry; itemID++)
    {
        ItemPrototype const* prototype = sObjectMgr.GetItemPrototype(itemID);
        bar.step();
        if (prototype == NULL)
            continue;

        switch (prototype->Bonding)
        {
        case 0:
            if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_BIND_NO))
                continue;
            break;
        case 1:
            if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_BIND_PICKUP))
                continue;
            break;
        case 2:
            if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_BIND_EQUIP))
                continue;
            break;
        case 3:
            if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_BIND_USE))
                continue;
            break;
        case 4:
            if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_BIND_QUEST))
                continue;
            break;
        default:
            continue;
            break;
        }

        if(sWorld.getConfig(CONFIG_BOOL_AHBOT_BUYPRICE_SELLER))
        {
            if(prototype->BuyPrice == 0)
                continue;
        }
        else
        {
            if (prototype->SellPrice == 0)
                continue;
        }

        if ((prototype->Quality < 0) || (prototype->Quality > 6))
            continue;

        if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_ITEMS_VENDOR))
        {
            bool isVendorItem = false;

            for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorItem); i++)
            {
                if (itemID == npcItems[i])
                    isVendorItem = true;
            }

            if (isVendorItem)
                continue;
        }

        if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_ITEMS_LOOT))
        {
            bool isLootItem = false;

            for (unsigned int i = 0; (i < lootItems.size()) && (!isLootItem); i++)
            {
                if (itemID == lootItems[i])
                    isLootItem = true;
            }

            if (isLootItem)
                continue;
        }

        if (!sWorld.getConfig(CONFIG_BOOL_AHBOT_ITEMS_MISC))
        {
            bool isVendorItem = false;
            bool isLootItem = false;

            for (unsigned int i = 0; (i < npcItems.size()) && (!isVendorItem); i++)
            {
                if (itemID == npcItems[i])
                    isVendorItem = true;
            }
            for (unsigned int i = 0; (i < lootItems.size()) && (!isLootItem); i++)
            {
                if (itemID == lootItems[i])
                    isLootItem = true;
            }
            if ((!isLootItem) && (!isVendorItem))
                continue;
        }

        if ((prototype->Class==ITEM_CLASS_ARMOR)||(prototype->Class==ITEM_CLASS_WEAPON)||(prototype->Class==ITEM_CLASS_ARMOR))
        {
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_ITEM_LEVEL)) > 0) && (prototype->ItemLevel < sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_ITEM_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_ITEM_LEVEL)) > 0) && (prototype->ItemLevel > sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_ITEM_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_REQ_LEVEL)) > 0) && (prototype->RequiredLevel < sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_REQ_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_REQ_LEVEL)) > 0) && (prototype->RequiredLevel > sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_REQ_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_SKILL_RANK)) > 0) && (prototype->RequiredSkill < sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_SKILL_RANK))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_SKILL_RANK)) > 0) && (prototype->RequiredSkill > sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_SKILL_RANK))) continue;
        }

        if ((prototype->Class==ITEM_CLASS_RECIPE)||(prototype->Class==ITEM_CLASS_CONSUMABLE)||(prototype->Class==ITEM_CLASS_PROJECTILE))
        {
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_REQ_LEVEL)) > 0) && (prototype->RequiredLevel < sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_REQ_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_REQ_LEVEL)) > 0) && (prototype->RequiredLevel > sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_REQ_LEVEL))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_SKILL_RANK)) > 0) && (prototype->RequiredSkill < sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MIN_SKILL_RANK))) continue;
            if (((sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_SKILL_RANK)) > 0) && (prototype->RequiredSkill > sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_MAX_SKILL_RANK))) continue;
        }

        ItemPool[prototype->Quality][prototype->Class].push_back(itemID);
        ItemAdded = true;

    }
    LoadConfig();
    if (!ItemAdded)
    {
        sLog.outString("AuctionHouseBot> Error, no items from xxxx_loot_template tables.");
        sLog.outString("AuctionHouseBot> AHBot is disabled!");
        sWorld.setConfig(CONFIG_UINT32_AHBOT_ALLIANCE_RATIO, 0);
        sWorld.setConfig(CONFIG_UINT32_AHBOT_HORDE_RATIO, 0);
        sWorld.setConfig(CONFIG_UINT32_AHBOT_NEUTRAL_RATIO, 0);
        return;
    }
    sLog.outString("\nItems loaded\tGrey\tWhite\tGreen\tBlue\tPurple\tOrange\tYellow");
    for (uint32 i=0; i<MAX_ITEM_CLASS;++i)
    {
        sLog.outString("%-11s\t%u\t%u\t%u\t%u\t%u\t%u\t%u",AllianceConfig.ItemInfos[0].ItemClassInfos[i].GetName().c_str(), ItemPool[0][i].size(),ItemPool[1][i].size(),ItemPool[2][i].size(),ItemPool[3][i].size(),ItemPool[4][i].size(),ItemPool[5][i].size(),ItemPool[6][i].size());
    }
    sLog.outString("\nAHBot> [AHBot-beta] is now loaded");
}

void AuctionHouseBot::Commands(uint32 command, uint32 ahMapID, uint32 col, char* args)
{
    AHBConfig *config;
    switch (ahMapID)
    {
    case 2:
        config = &AllianceConfig;
        break;
    case 6:
        config = &HordeConfig;
        break;
    case 7:
        config = &NeutralConfig;
        break;
    }
    std::string color;
    switch (col)
    {
    case AHB_GREY:
        color = "grey";
        break;
    case AHB_WHITE:
        color = "white";
        break;
    case AHB_GREEN:
        color = "green";
        break;
    case AHB_BLUE:
        color = "blue";
        break;
    case AHB_PURPLE:
        color = "purple";
        break;
    case AHB_ORANGE:
        color = "orange";
        break;
    case AHB_YELLOW:
        color = "yellow";
        break;
    default:
        break;
    }
    //switch (command)
}

void AuctionHouseBot::LoadSellerValues(AHBConfig& config)
{
    config.ItemInfos[E_GREY].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_GREY_AMOUNT));
    config.ItemInfos[E_WHITE].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_WHITE_AMOUNT));
    config.ItemInfos[E_GREEN].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_GREEN_AMOUNT));
    config.ItemInfos[E_BLUE].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_BLUE_AMOUNT));
    config.ItemInfos[E_PURPLE].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_PURPLE_AMOUNT));
    config.ItemInfos[E_ORANGE].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_ORANGE_AMOUNT));
    config.ItemInfos[E_YELLOW].SetAmountOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_ITEM_YELLOW_AMOUNT));
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_REAGENT_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_REAGENT_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GENERIC_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_GENERIC_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_KEY_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_KEY_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT,10);
    if (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GLYPH_AMOUNT)>10) sWorld.setConfig(CONFIG_UINT32_AHBOT_CLASS_GLYPH_AMOUNT,10);

    for (uint32 j=0; j<AHB_QUALITY_MAX; ++j)
    {
        switch ((e_ahb_quality) j)
        {
        case E_GREY :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        case E_WHITE :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_REAGENT_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GENERIC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_KEY_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GLYPH_AMOUNT));
            break;
        case E_GREEN :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_KEY_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        case E_BLUE :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        case E_PURPLE :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        case E_ORANGE :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        case E_YELLOW :
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONSUMABLE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_CONTAINER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_WEAPON].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GEM].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_ARMOR].SetQuantityOfItems(sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT));
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_REAGENT].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_PROJECTILE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_TRADE_GOODS].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GENERIC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_RECIPE].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUIVER].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_QUEST].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_KEY].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_MISC].SetQuantityOfItems(0);
            config.ItemInfos[j].ItemClassInfos[ITEM_CLASS_GLYPH].SetQuantityOfItems(0);
            break;
        }
    }

    for (uint32 j=0; j<AHB_QUALITY_MAX; ++j)
    {
        uint32 indice = config.ItemInfos[j].GetAmountOfItems()/
            (sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONSUMABLE_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_CONTAINER_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_WEAPON_AMOUNT) +
            sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GEM_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_ARMOR_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_REAGENT_AMOUNT) +
            sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_PROJECTILE_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_TRADEGOOD_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GENERIC_AMOUNT) +
            sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_RECIPE_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUIVER_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_QUEST_AMOUNT) +
            sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_KEY_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_MISC_AMOUNT) + sWorld.getConfig(CONFIG_UINT32_AHBOT_CLASS_GLYPH_AMOUNT));
        for (uint32 i=0;i<MAX_ITEM_CLASS;++i)
        {
            config.ItemInfos[j].ItemClassInfos[i].SetAmountOfItems(indice);
        }
    }
    uint32 PriceRatio;
    switch(config.GetAHID())
    {
    case 2: PriceRatio = sWorld.getConfig(CONFIG_UINT32_AHBOT_ALLIENCE_PRICE_RATIO); break;
    case 6: PriceRatio = sWorld.getConfig(CONFIG_UINT32_AHBOT_HORDE_PRICE_RATIO); break;
    case 7: PriceRatio = sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_PRICE_RATIO); break;
    default:
        PriceRatio = sWorld.getConfig(CONFIG_UINT32_AHBOT_NEUTRAL_PRICE_RATIO);
        break;
    }
    config.ItemInfos[E_GREY].SetMaxPrice(10+PriceRatio);
    config.ItemInfos[E_WHITE].SetMaxPrice(15+PriceRatio);
    config.ItemInfos[E_GREEN].SetMaxPrice(25+PriceRatio);
    config.ItemInfos[E_BLUE].SetMaxPrice(40+PriceRatio);
    config.ItemInfos[E_PURPLE].SetMaxPrice(60+PriceRatio);
    config.ItemInfos[E_ORANGE].SetMaxPrice(85+PriceRatio);
    config.ItemInfos[E_YELLOW].SetMaxPrice(95+PriceRatio);
    config.ItemInfos[E_GREY].SetMinPrice(5+PriceRatio);
    config.ItemInfos[E_WHITE].SetMinPrice(10+PriceRatio);
    config.ItemInfos[E_GREEN].SetMinPrice(15+PriceRatio);
    config.ItemInfos[E_BLUE].SetMinPrice(25+PriceRatio);
    config.ItemInfos[E_PURPLE].SetMinPrice(35+PriceRatio);
    config.ItemInfos[E_ORANGE].SetMinPrice(45+PriceRatio);
    config.ItemInfos[E_YELLOW].SetMinPrice(55+PriceRatio);
    config.ItemInfos[E_GREY].SetMaxBidPrice(10+PriceRatio);
    config.ItemInfos[E_WHITE].SetMaxBidPrice(10+PriceRatio);
    config.ItemInfos[E_GREEN].SetMaxBidPrice(10+PriceRatio);
    config.ItemInfos[E_BLUE].SetMaxBidPrice(20+PriceRatio);
    config.ItemInfos[E_PURPLE].SetMaxBidPrice(30+PriceRatio);
    config.ItemInfos[E_ORANGE].SetMaxBidPrice(40+PriceRatio);
    config.ItemInfos[E_YELLOW].SetMaxBidPrice(50+PriceRatio);
    config.ItemInfos[E_GREY].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_WHITE].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_GREEN].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_BLUE].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_PURPLE].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_ORANGE].SetMinBidPrice(5+PriceRatio);
    config.ItemInfos[E_YELLOW].SetMinBidPrice(5+PriceRatio);
    //load min and max auction times
    config.SetMinTime(sWorld.getConfig(CONFIG_UINT32_AHBOT_MINTIME));
    config.SetMaxTime(sWorld.getConfig(CONFIG_UINT32_AHBOT_MAXTIME));
    if (debug_Out)
    {
        sLog.outString("minTime = %u", config.GetMinTime());
        sLog.outString("maxTime = %u", config.GetMaxTime());
    }
    //load percentages

    if (debug_Out)
    {
        sLog.outString("GreyItems = %u", config.ItemInfos[E_GREY].GetAmountOfItems());
        sLog.outString("WhiteItems = %u", config.ItemInfos[E_WHITE].GetAmountOfItems());
        sLog.outString("GreenItems = %u", config.ItemInfos[E_GREEN].GetAmountOfItems());
        sLog.outString("BlueItems = %u", config.ItemInfos[E_BLUE].GetAmountOfItems());
        sLog.outString("PurpleItems = %u", config.ItemInfos[E_PURPLE].GetAmountOfItems());
        sLog.outString("OrangeItems = %u", config.ItemInfos[E_ORANGE].GetAmountOfItems());
        sLog.outString("YellowItems = %u", config.ItemInfos[E_YELLOW].GetAmountOfItems());
    }

}

void AuctionHouseBot::LoadBuyerValues(AHBConfig& config)
{
    //load buyer bid prices
    config.ItemInfos[AHB_GREY].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_GREY));
    config.ItemInfos[AHB_WHITE].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_WHITE));
    config.ItemInfos[AHB_GREEN].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_GREEN));
    config.ItemInfos[AHB_BLUE].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_BLUE));
    config.ItemInfos[AHB_PURPLE].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_PURPLE));
    config.ItemInfos[AHB_ORANGE].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_ORANGE));
    config.ItemInfos[AHB_YELLOW].SetBuyerPrice(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_PRICE_YELLOW));

    if (debug_Out)
    {
        sLog.outString("buyerPriceGrey = %u",   config.ItemInfos[AHB_GREY].GetBuyerPrice());
        sLog.outString("buyerPriceWhite = %u",  config.ItemInfos[AHB_WHITE].GetBuyerPrice());
        sLog.outString("buyerPriceGreen = %u",  config.ItemInfos[AHB_GREEN].GetBuyerPrice());
        sLog.outString("buyerPriceBlue = %u",   config.ItemInfos[AHB_BLUE].GetBuyerPrice());
        sLog.outString("buyerPricePurple = %u", config.ItemInfos[AHB_PURPLE].GetBuyerPrice());
        sLog.outString("buyerPriceOrange = %u", config.ItemInfos[AHB_ORANGE].GetBuyerPrice());
        sLog.outString("buyerPriceYellow = %u", config.ItemInfos[AHB_YELLOW].GetBuyerPrice());
    }
    //load bidding interval
    config.SetBiddingInterval(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_BIDDIGIN_INTERVAL));
    if (debug_Out)
    {
        sLog.outString("buyerBiddingInterval = %u", config.GetBiddingInterval());
    }
    //load bids per interval
    config.SetBidsPerInterval(sWorld.getConfig(CONFIG_UINT32_AHBOT_BUYER_BID_INTERVAL));
    if (debug_Out)
    {
        sLog.outString("buyerBidsPerInterval = %u", config.GetBidsPerInterval());
    }

}
