#define private public // make Directory::m_values available to clean it.
#include "directory.h"
#undef private

#include "gtest/gtest.h"
#include "mock_table.h"
#include "ut_helper.h"
#include "mock_orchagent_main.h"
#include "txmonorch.h"

namespace txmonorch_test 
{
    using namespace std;

    // copy from portorch_ut.cpp, logic to mock some sai APIs.

        // SAI default ports
    std::map<std::string, std::vector<swss::FieldValueTuple>> defaultPortList;

    sai_port_api_t ut_sai_port_api;
    sai_port_api_t *pold_sai_port_api;
    sai_switch_api_t ut_sai_switch_api;
    sai_switch_api_t *pold_sai_switch_api;

    bool not_support_fetching_fec;
    uint32_t _sai_set_port_fec_count;
    int32_t _sai_port_fec_mode;
    vector<sai_port_fec_mode_t> mock_port_fec_modes = {SAI_PORT_FEC_MODE_RS, SAI_PORT_FEC_MODE_FC};

    sai_status_t _ut_stub_sai_get_port_attribute(
        _In_ sai_object_id_t port_id,
        _In_ uint32_t attr_count,
        _Inout_ sai_attribute_t *attr_list)
    {
        sai_status_t status;
        if (attr_count == 1 && attr_list[0].id == SAI_PORT_ATTR_SUPPORTED_FEC_MODE)
        {
            if (not_support_fetching_fec)
            {
                status = SAI_STATUS_NOT_IMPLEMENTED;
            }
            else
            {
                uint32_t i;
                for (i = 0; i < attr_list[0].value.s32list.count && i < mock_port_fec_modes.size(); i++)
                {
                    attr_list[0].value.s32list.list[i] = mock_port_fec_modes[i];
                }
                attr_list[0].value.s32list.count = i;
                status = SAI_STATUS_SUCCESS;
            }
        }
        else if (attr_count == 1 && attr_list[0].id == SAI_PORT_ATTR_OPER_PORT_FEC_MODE)
        {
            attr_list[0].value.s32 = _sai_port_fec_mode;
            status = SAI_STATUS_SUCCESS;
        }
        else
        {
            status = pold_sai_port_api->get_port_attribute(port_id, attr_count, attr_list);
        }
        return status;
    }

    uint32_t _sai_set_pfc_mode_count;
    uint32_t _sai_set_admin_state_up_count;
    uint32_t _sai_set_admin_state_down_count;
    sai_status_t _ut_stub_sai_set_port_attribute(
        _In_ sai_object_id_t port_id,
        _In_ const sai_attribute_t *attr)
    {
        if (attr[0].id == SAI_PORT_ATTR_FEC_MODE)
        {
            _sai_set_port_fec_count++;
            _sai_port_fec_mode = attr[0].value.s32;
        }
        else if (attr[0].id == SAI_PORT_ATTR_AUTO_NEG_MODE)
        {
            /* Simulating failure case */
            return SAI_STATUS_FAILURE;
        }
	else if (attr[0].id == SAI_PORT_PRIORITY_FLOW_CONTROL_MODE_COMBINED)
	{
	    _sai_set_pfc_mode_count++;
        }
	else if (attr[0].id == SAI_PORT_ATTR_ADMIN_STATE)
	{
            if (attr[0].value.booldata) {
	        _sai_set_admin_state_up_count++;
            } else {
	        _sai_set_admin_state_down_count++;
            }
        }
        return pold_sai_port_api->set_port_attribute(port_id, attr);
    }

    uint32_t *_sai_syncd_notifications_count;
    int32_t *_sai_syncd_notification_event;
    uint32_t _sai_switch_dlr_packet_action_count;
    uint32_t _sai_switch_dlr_packet_action;
    sai_status_t _ut_stub_sai_set_switch_attribute(
        _In_ sai_object_id_t switch_id,
        _In_ const sai_attribute_t *attr)
    {
        if (attr[0].id == SAI_REDIS_SWITCH_ATTR_NOTIFY_SYNCD)
        {
            *_sai_syncd_notifications_count =+ 1;
            *_sai_syncd_notification_event = attr[0].value.s32;
        }
	else if (attr[0].id == SAI_SWITCH_ATTR_PFC_DLR_PACKET_ACTION)
        {
	    _sai_switch_dlr_packet_action_count++;
	    _sai_switch_dlr_packet_action = attr[0].value.s32;
	}
        return pold_sai_switch_api->set_switch_attribute(switch_id, attr);
    }

    void _hook_sai_port_api()
    {
        ut_sai_port_api = *sai_port_api;
        pold_sai_port_api = sai_port_api;
        ut_sai_port_api.get_port_attribute = _ut_stub_sai_get_port_attribute;
        ut_sai_port_api.set_port_attribute = _ut_stub_sai_set_port_attribute;
        sai_port_api = &ut_sai_port_api;
    }

    void _unhook_sai_port_api()
    {
        sai_port_api = pold_sai_port_api;
    }

    void _hook_sai_switch_api()
    {
        ut_sai_switch_api = *sai_switch_api;
        pold_sai_switch_api = sai_switch_api;
        ut_sai_switch_api.set_switch_attribute = _ut_stub_sai_set_switch_attribute;
        sai_switch_api = &ut_sai_switch_api;
    }

    void _unhook_sai_switch_api()
    {
        sai_switch_api = pold_sai_switch_api;
    }

    sai_queue_api_t ut_sai_queue_api;
    sai_queue_api_t *pold_sai_queue_api;
    int _sai_set_queue_attr_count = 0;

    sai_status_t _ut_stub_sai_set_queue_attribute(sai_object_id_t queue_id, const sai_attribute_t *attr)
    {
        if(attr->id == SAI_QUEUE_ATTR_PFC_DLR_INIT)
        {
            if(attr->value.booldata == true)
            {
                _sai_set_queue_attr_count++;
            }
            else
            {
                _sai_set_queue_attr_count--;
            }
        }
        return SAI_STATUS_SUCCESS;
    }


    // Define a test fixture class
    struct TxMonOrchTest : public ::testing::Test 
    {
        shared_ptr<swss::DBConnector> m_app_db;
        shared_ptr<swss::DBConnector> m_config_db;
        shared_ptr<swss::DBConnector> m_state_db;
        shared_ptr<swss::DBConnector> m_counters_db;
        shared_ptr<swss::DBConnector> m_chassis_app_db;
        shared_ptr<swss::DBConnector> m_asic_db;

        TxMonOrchTest()
        {
            // again, logics come from portorch_ut.cpp
            
            // FIXME: move out from constructor
            m_app_db = make_shared<swss::DBConnector>(
                "APPL_DB", 0);
            m_counters_db = make_shared<swss::DBConnector>(
                "COUNTERS_DB", 0);
            m_config_db = make_shared<swss::DBConnector>(
                "CONFIG_DB", 0);
            m_state_db = make_shared<swss::DBConnector>(
                "STATE_DB", 0);
            m_chassis_app_db = make_shared<swss::DBConnector>(
                "CHASSIS_APP_DB", 0);
            m_asic_db = make_shared<swss::DBConnector>(
                "ASIC_DB", 0);
        }

        virtual void SetUp() override
        {
            ::testing_db::reset();

            // Create dependencies ...
            TableConnector stateDbSwitchTable(m_state_db.get(), "SWITCH_CAPABILITY");
            TableConnector app_switch_table(m_app_db.get(), APP_SWITCH_TABLE_NAME);
            TableConnector conf_asic_sensors(m_config_db.get(), CFG_ASIC_SENSORS_TABLE_NAME);

            vector<TableConnector> switch_tables = {
                conf_asic_sensors,
                app_switch_table
            };

            ASSERT_EQ(gSwitchOrch, nullptr);
            gSwitchOrch = new SwitchOrch(m_app_db.get(), switch_tables, stateDbSwitchTable);

            const int portsorch_base_pri = 40;

            vector<table_name_with_pri_t> ports_tables = {
                { APP_PORT_TABLE_NAME, portsorch_base_pri + 5 },
                { APP_SEND_TO_INGRESS_PORT_TABLE_NAME, portsorch_base_pri + 5 },
                { APP_VLAN_TABLE_NAME, portsorch_base_pri + 2 },
                { APP_VLAN_MEMBER_TABLE_NAME, portsorch_base_pri },
                { APP_LAG_TABLE_NAME, portsorch_base_pri + 4 },
                { APP_LAG_MEMBER_TABLE_NAME, portsorch_base_pri }
            };

            ASSERT_EQ(gPortsOrch, nullptr);

            gPortsOrch = new PortsOrch(m_app_db.get(), m_state_db.get(), ports_tables, m_chassis_app_db.get());

            vector<string> flex_counter_tables = {
                CFG_FLEX_COUNTER_TABLE_NAME
            };
            auto* flexCounterOrch = new FlexCounterOrch(m_config_db.get(), flex_counter_tables);
            gDirectory.set(flexCounterOrch);

            vector<string> buffer_tables = { APP_BUFFER_POOL_TABLE_NAME,
                                             APP_BUFFER_PROFILE_TABLE_NAME,
                                             APP_BUFFER_QUEUE_TABLE_NAME,
                                             APP_BUFFER_PG_TABLE_NAME,
                                             APP_BUFFER_PORT_INGRESS_PROFILE_LIST_NAME,
                                             APP_BUFFER_PORT_EGRESS_PROFILE_LIST_NAME };

            //ASSERT_EQ(gBufferOrch, nullptr);
            //gBufferOrch = new BufferOrch(m_app_db.get(), m_config_db.get(), m_state_db.get(), buffer_tables);

            ASSERT_EQ(gIntfsOrch, nullptr);
            gIntfsOrch = new IntfsOrch(m_app_db.get(), APP_INTF_TABLE_NAME, gVrfOrch, m_chassis_app_db.get());

            const int fdborch_pri = 20;

            vector<table_name_with_pri_t> app_fdb_tables = {
                { APP_FDB_TABLE_NAME,        FdbOrch::fdborch_pri},
                { APP_VXLAN_FDB_TABLE_NAME,  FdbOrch::fdborch_pri},
                { APP_MCLAG_FDB_TABLE_NAME,  fdborch_pri}
            };

            TableConnector stateDbFdb(m_state_db.get(), STATE_FDB_TABLE_NAME);
            TableConnector stateMclagDbFdb(m_state_db.get(), STATE_MCLAG_REMOTE_FDB_TABLE_NAME);
            ASSERT_EQ(gFdbOrch, nullptr);
            gFdbOrch = new FdbOrch(m_app_db.get(), app_fdb_tables, stateDbFdb, stateMclagDbFdb, gPortsOrch);

            ASSERT_EQ(gNeighOrch, nullptr);
            gNeighOrch = new NeighOrch(m_app_db.get(), APP_NEIGH_TABLE_NAME, gIntfsOrch, gFdbOrch, gPortsOrch, m_chassis_app_db.get());

            vector<string> qos_tables = {
                CFG_TC_TO_QUEUE_MAP_TABLE_NAME,
                CFG_SCHEDULER_TABLE_NAME,
                CFG_DSCP_TO_TC_MAP_TABLE_NAME,
                CFG_MPLS_TC_TO_TC_MAP_TABLE_NAME,
                CFG_DOT1P_TO_TC_MAP_TABLE_NAME,
                CFG_QUEUE_TABLE_NAME,
                CFG_PORT_QOS_MAP_TABLE_NAME,
                CFG_WRED_PROFILE_TABLE_NAME,
                CFG_TC_TO_PRIORITY_GROUP_MAP_TABLE_NAME,
                CFG_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP_TABLE_NAME,
                CFG_PFC_PRIORITY_TO_QUEUE_MAP_TABLE_NAME,
                CFG_DSCP_TO_FC_MAP_TABLE_NAME,
                CFG_EXP_TO_FC_MAP_TABLE_NAME,
                CFG_TC_TO_DSCP_MAP_TABLE_NAME
            };
            gQosOrch = new QosOrch(m_config_db.get(), qos_tables);

            vector<string> pfc_wd_tables = {
                CFG_PFC_WD_TABLE_NAME
            };

            static const vector<sai_port_stat_t> portStatIds =
            {
                SAI_PORT_STAT_PFC_0_RX_PKTS,
                SAI_PORT_STAT_PFC_1_RX_PKTS,
                SAI_PORT_STAT_PFC_2_RX_PKTS,
                SAI_PORT_STAT_PFC_3_RX_PKTS,
                SAI_PORT_STAT_PFC_4_RX_PKTS,
                SAI_PORT_STAT_PFC_5_RX_PKTS,
                SAI_PORT_STAT_PFC_6_RX_PKTS,
                SAI_PORT_STAT_PFC_7_RX_PKTS,
                SAI_PORT_STAT_PFC_0_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_1_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_2_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_3_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_4_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_5_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_6_ON2OFF_RX_PKTS,
                SAI_PORT_STAT_PFC_7_ON2OFF_RX_PKTS,
            };

            static const vector<sai_queue_stat_t> queueStatIds =
            {
                SAI_QUEUE_STAT_PACKETS,
                SAI_QUEUE_STAT_CURR_OCCUPANCY_BYTES,
            };

            static const vector<sai_queue_attr_t> queueAttrIds =
            {
                SAI_QUEUE_ATTR_PAUSE_STATUS,
            };
            ASSERT_EQ((gPfcwdOrch<PfcWdDlrHandler, PfcWdDlrHandler>), nullptr);
            gPfcwdOrch<PfcWdDlrHandler, PfcWdDlrHandler> = new PfcWdSwOrch<PfcWdDlrHandler, PfcWdDlrHandler>(m_config_db.get(), pfc_wd_tables, portStatIds, queueStatIds, queueAttrIds, 100);

        }

        virtual void TearDown() override
        {
            ::testing_db::reset();

            auto buffer_maps = BufferOrch::m_buffer_type_maps;
            for (auto &i : buffer_maps)
            {
                i.second->clear();
            }

            delete gNeighOrch;
            gNeighOrch = nullptr;
            delete gFdbOrch;
            gFdbOrch = nullptr;
            delete gIntfsOrch;
            gIntfsOrch = nullptr;
            delete gPortsOrch;
            gPortsOrch = nullptr;
            delete gBufferOrch;
            gBufferOrch = nullptr;
            delete gPfcwdOrch<PfcWdDlrHandler, PfcWdDlrHandler>;
            gPfcwdOrch<PfcWdDlrHandler, PfcWdDlrHandler> = nullptr;
            delete gQosOrch;
            gQosOrch = nullptr;
            delete gSwitchOrch;
            gSwitchOrch = nullptr;

            // clear orchs saved in directory
            gDirectory.m_values.clear();
        }

        static void SetUpTestCase()
        {
            // Init switch and create dependencies

            map<string, string> profile = {
                { "SAI_VS_SWITCH_TYPE", "SAI_VS_SWITCH_TYPE_BCM56850" },
                { "KV_DEVICE_MAC_ADDRESS", "20:03:04:05:06:00" }
            };

            auto status = ut_helper::initSaiApi(profile);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            sai_attribute_t attr;

            attr.id = SAI_SWITCH_ATTR_INIT_SWITCH;
            attr.value.booldata = true;

            status = sai_switch_api->create_switch(&gSwitchId, 1, &attr);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            // Get switch source MAC address
            attr.id = SAI_SWITCH_ATTR_SRC_MAC_ADDRESS;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);

            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            gMacAddress = attr.value.mac;

            // Get the default virtual router ID
            attr.id = SAI_SWITCH_ATTR_DEFAULT_VIRTUAL_ROUTER_ID;
            status = sai_switch_api->get_switch_attribute(gSwitchId, 1, &attr);

            ASSERT_EQ(status, SAI_STATUS_SUCCESS);

            gVirtualRouterId = attr.value.oid;

            // Get SAI default ports
            defaultPortList = ut_helper::getInitialSaiPorts();
            ASSERT_TRUE(!defaultPortList.empty());
        }

        static void TearDownTestCase()
        {
            auto status = sai_switch_api->remove_switch(gSwitchId);
            ASSERT_EQ(status, SAI_STATUS_SUCCESS);
            gSwitchId = 0;

            ut_helper::uninitSaiApi();
        }
    };

    // We don't have API for checking period and threshold independtly
    TEST_F(TxMonOrchTest, pollOnePortErrorStatisticsTest) {
        TableConnector stateDbTxErr(m_state_db.get(), /*"TX_ERR_STATE"*/STATE_TX_ERR_TABLE_NAME);
        TableConnector applDbTxErr(m_app_db.get(), /*"TX_ERR_APPL"*/APP_TX_ERR_TABLE_NAME);
        TableConnector confDbTxErr(m_config_db.get(), /*"TX_ERR_CFG"*/CFG_PORT_TX_ERR_TABLE_NAME);
        TxMonOrch txMonOrch(applDbTxErr, confDbTxErr, stateDbTxErr);
        TxErrorStatistics stat;
        vector<FieldValueTuple> period {{TXMONORCH_FIELD_CFG_PERIOD, "1"}};
        vector<FieldValueTuple> threshold {{TXMONORCH_FIELD_CFG_THRESHOLD, "10"}};
        txMonOrch.pollOnePortErrorStatistics("2", stat);
        // cout << tesState(stat) << " ";
        // cout << tesPortId(stat) << " ";
        // cout << tesStatistics(stat) << " ";
        // cout << tesThreshold(stat) << " ";
        EXPECT_TRUE(tesState(stat) == TXMONORCH_PORT_STATE_OK);
        EXPECT_TRUE(tesPortId(stat) == 1);
        EXPECT_TRUE(tesStatistics(stat) == 0);
        EXPECT_TRUE(tesThreshold(stat) == 10);
    }

}
