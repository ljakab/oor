/*
 *
 * Copyright (C) 2011, 2015 Cisco Systems, Inc.
 * Copyright (C) 2015 CBA research group, Technical University of Catalonia.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package org.openoverlayrouter.noroot;

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.os.Bundle;
import android.os.Environment;
import android.support.v4.app.Fragment;
import android.support.v4.app.FragmentActivity;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.Toast;

import com.rengwuxian.materialedittext.MaterialEditText;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;
import java.util.Map;


public class updateConfActivity extends Fragment {

    public static final String confFile = "oor.conf";
    public static int iid = 0;
    public static String eidIPv4 = "";
    public static String eidIPv6 = "";
    public static List<String> ifaces = null;
    public static String MR = "";
    public static String MS = "";
    public static String MSKey = "";
    public static String proxyETR = "";
    public static String DNS1 = "";
    public static String DNS2 = "";
    public static boolean overrideDNS = false;
    public static boolean nat_aware = false;
    public static int rloc_prob_interval = 30;
    public static int rloc_prob_retries = 2;
    public static int rloc_prob_retries_interval = 5;
    public static String encapsulation = "LISP";
    public static Map<String, Integer> encapMap;
    public static String logLevel = "1";
    public static final int CONFIG_UPDATED = 1;
    public static File conf_file = null;
    public static String log_file = "";
    public static List<String> iface_list = null;
    private static File sdcardDir = Environment.getExternalStorageDirectory();

    private LinearLayout llLayout;
    private FragmentActivity faActivity;

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {

        faActivity = (FragmentActivity) super.getActivity();
        llLayout = (LinearLayout) inflater.inflate(R.layout.updateconf2, container, false);
        setTextHintAlphas();
        conf_file = new File(sdcardDir, confFile);
        log_file = sdcardDir.getAbsolutePath() + "/oor.log";
        encapMap = ConfigTools.createEncapMap(getResources().getStringArray(R.array.EncapsulationArray));
        iface_list = ConfigTools.get_ifaces_list();


        MultiSelectionSpinner spinner = (MultiSelectionSpinner) llLayout.findViewById(R.id.IfaceNameSpinner);
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(faActivity, android.R.layout.simple_spinner_item, iface_list);
        // Specify the layout to use when the list of choices appears
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        // Apply the adapter to the spinner
        spinner.setItems(iface_list);

        Spinner encapSpinner = (Spinner) llLayout.findViewById(R.id.EncapsulationSpinner);
        ArrayAdapter<CharSequence> encapAdapter = ArrayAdapter.createFromResource(faActivity, R.array.EncapsulationArray, android.R.layout.simple_spinner_item);
        encapSpinner.setAdapter(encapAdapter);

        Spinner logSpinner = (Spinner) llLayout.findViewById(R.id.LogSpinner);
        ArrayAdapter<CharSequence> logAdapter = ArrayAdapter.createFromResource(faActivity, R.array.LogLevelArray, android.R.layout.simple_spinner_item);
        logSpinner.setAdapter(logAdapter);

        if (!conf_file.exists()) {
            createConfFile();
        }

        readConfFileAndFillParameters();

        Button updateConfButton = (Button) llLayout.findViewById(R.id.updateConfButton);
        updateConfButton.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                updateConfClicked(llLayout);
            }
        });

        CheckBox updateConf_NAT_aware = (CheckBox) llLayout.findViewById(R.id.updateConf_NAT_aware);
        updateConf_NAT_aware.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                updateConfNATAwareClicked(llLayout);
            }
        });

        CheckBox updateCondDNSCheck = (CheckBox) llLayout.findViewById(R.id.updateConfDNSCheck);
        updateCondDNSCheck.setOnClickListener(new View.OnClickListener() {
            public void onClick(View view) {
                updateConfDNSClicked(llLayout);
            }
        });

        return llLayout;

    }

    public Boolean lowLogLevel(){
        try {
            conf_file = new File(sdcardDir, confFile);
            BufferedReader br = new BufferedReader(new FileReader(conf_file));
            String line = br.readLine();
            while (line != null) {
                if (line.startsWith("#")) {
                    line = br.readLine();
                    continue;
                }
                line = line.toLowerCase();
                line = line.replaceAll("\\s", "");

                if (line.contains("debug=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        if(tmp[1].equals("0")) return true;
                        else return false;
                    }
                }
                line = br.readLine();
            }
        } catch (IOException e) {
            Log.e("updateconf", "An IOException was caught :" + e.getMessage());
        }
        return false;
    }

    public void readConfFileAndFillParameters() {

        try {

            BufferedReader br = new BufferedReader(new FileReader(conf_file));
            String line = br.readLine();
            String sub_line = null;
            String sub_line_1 = null;
            ifaces = new ArrayList<String>();

            while (line != null) {
                if (line.startsWith("#")) {
                    line = br.readLine();
                    continue;
                }
                line = line.toLowerCase();
                line = line.replaceAll("\\s", "");

                if (line.contains("database-mapping")) {
                    int num_braces = 0;
                    if (line.contains("{")) {
                        num_braces++;
                    }
                    do {
                        sub_line = br.readLine();
                        if (sub_line.startsWith("#")) {
                            sub_line = br.readLine();
                            continue;
                        }
                        if (sub_line.contains("{")) {
                            num_braces++;
                        }

                        sub_line = sub_line.toLowerCase();
                        sub_line = sub_line.replaceAll("\\s", "");
                        if (sub_line.contains("eid-prefix")) {
                            String[] tmp = sub_line.split("=");
                            if (tmp.length < 2)
                                continue;
                            String[] tmp_1 = tmp[1].split("/");
                            if (tmp_1.length < 2)
                                continue;
                            if (tmp_1[0].contains(":")) {
                                eidIPv6 = tmp_1[0];
                                EditText e = (EditText) llLayout.findViewById(R.id.updateConfeid6Text);
                                e.setText(eidIPv6);
                            } else if (tmp_1[0].contains(".")) {
                                eidIPv4 = tmp_1[0];
                                EditText e = (EditText) llLayout.findViewById(R.id.updateConfeid4Text);
                                e.setText(eidIPv4);
                            }
                        }
                        if (sub_line.contains("iid")) {
                            String[] tmp = sub_line.split("=");
                            if (tmp.length < 2)
                                continue;
                            String iidString = tmp[1];
                            iid = ConfigTools.getInstanceId(iidString);
                            if (iid == -1 || iid == -2)
                                iid = 0;
                            EditText e = (EditText) llLayout.findViewById(R.id.updateConfiidText);
                            e.setText(String.valueOf(iid));
                        }
                        if (sub_line.contains("rloc-iface")) {
                            sub_line = br.readLine();
                            if (sub_line.startsWith("#")) {
                                sub_line = br.readLine();
                                continue;
                            }
                            sub_line = sub_line.toLowerCase();
                            sub_line = sub_line.replaceAll("\\s", "");

                            if (sub_line.contains("interface")) {
                                String[] tmp = sub_line.split("=");
                                if (tmp.length < 2)
                                    continue;
                                String iface_name = tmp[1];
                                Iterator<String> iface_it = iface_list.iterator();
                                while (iface_it.hasNext()) {
                                    if (iface_it.next().equals(iface_name)) {
                                        if (!ifaces.contains(iface_name)) {
                                            ifaces.add(iface_name);
                                        }
                                        break;
                                    }
                                }
                            }
                        }

                        if (sub_line.contains("}")) {
                            num_braces--;
                        }
                    } while (num_braces != 0);
                } else if (line.contains("map-resolver")) {
                    sub_line = br.readLine();
                    if (sub_line.startsWith("#")) {
                        sub_line = br.readLine();
                        continue;
                    }
                    sub_line = sub_line.replaceAll("\\s", "");

                    if (sub_line.contains(",")) {
                        String[] tmp = sub_line.split(",");
                        if (tmp.length != 0) {
                            MR = tmp[0];
                        }
                    } else {
                        MR = sub_line;
                    }

                    EditText e = (EditText) llLayout.findViewById(R.id.updateConfMRText);
                    e.setText(MR);
                } else if (line.contains("nat_traversal_support") && !line.startsWith("#")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        String nat_aware_aux = tmp[1];
                        if (nat_aware_aux.equals("on") || nat_aware_aux.equals("true")) {
                            nat_aware = true;
                        } else {
                            nat_aware = false;
                        }
                    }
                    CheckBox c = (CheckBox) llLayout.findViewById(R.id.updateConf_NAT_aware);
                    c.setChecked(nat_aware);
                } else if (line.contains("map-server")) {
                    do {
                        sub_line = br.readLine();
                        if (sub_line.startsWith("#")) {
                            sub_line = br.readLine();
                            continue;
                        }
                        sub_line = sub_line.replaceAll("\\s", "");
                        sub_line_1 = sub_line; // Not lose uppercase in password
                        sub_line = sub_line.toLowerCase();


                        if (sub_line.contains("address")) {
                            String[] tmp = sub_line.split("=");
                            if (tmp.length > 1) {
                                MS = tmp[1];
                            }
                        } else if (sub_line.contains("key") & !sub_line.contains("key-type")) {
                            String[] tmp = sub_line_1.split("=");
                            if (tmp.length > 1) {
                                MSKey = tmp[1];
                            }
                        }
                    } while (!sub_line.contains("}"));

                    EditText e = (EditText) llLayout.findViewById(R.id.updateConfMSText);
                    e.setText(MS);

                    EditText et = (EditText) llLayout.findViewById(R.id.updateConfMSKeyText);
                    et.setText(MSKey);
                } else if (line.contains("proxy-etr")) {
                    do {
                        sub_line = br.readLine();
                        if (sub_line.startsWith("#")) {
                            sub_line = br.readLine();
                            continue;
                        }
                        sub_line = sub_line.toLowerCase();
                        sub_line = sub_line.replaceAll("\\s", "");

                        if (sub_line.contains("address")) {
                            String[] tmp = sub_line.split("=");
                            if (tmp.length > 1) {
                                proxyETR = tmp[1];
                            }
                        }
                    } while (!sub_line.contains("}"));

                    EditText e = (EditText) llLayout.findViewById(R.id.updateConf_proxy_etr);
                    e.setText(proxyETR);

                } else if (line.contains("override-dns=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        String overrideDNS_aux = tmp[1];
                        if (overrideDNS_aux.equals("on") || overrideDNS_aux.equals("true")) {
                            overrideDNS = true;
                        } else {
                            overrideDNS = false;
                        }
                    }

                    CheckBox c = (CheckBox) llLayout.findViewById(R.id.updateConfDNSCheck);
                    c.setChecked(overrideDNS);
                } else if (line.contains("override-dns-primary=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        if (ConfigTools.validate_IP_Address(tmp[1])) {
                            DNS1 = tmp[1];
                        }
                    }
                    EditText e = (EditText) llLayout.findViewById(R.id.updateConfDNS1Text);
                    e.setText(DNS1);
                } else if (line.contains("override-dns-secondary=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        if (ConfigTools.validate_IP_Address(tmp[1])) {
                            DNS2 = tmp[1];
                        }
                    }
                    EditText e = (EditText) llLayout.findViewById(R.id.updateConfDNS2Text);
                    e.setText(DNS2);
                } else if (line.contains("encapsulation=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        if (ConfigTools.validEncapsulation(tmp[1])) {
                            encapsulation = tmp[1];
                        }
                    }
                    Spinner encap_spinner = (Spinner) llLayout.findViewById(R.id.EncapsulationSpinner);
                    encap_spinner.setSelection(encapMap.get(encapsulation));
                } else if (line.contains("debug=")) {
                    String[] tmp = line.split("=");
                    if (tmp.length > 1) {
                        logLevel = tmp[1];
                    }
                    Spinner log_spinner = (Spinner) llLayout.findViewById(R.id.LogSpinner);
                    log_spinner.setSelection(new Integer(logLevel).intValue());
                }

                line = br.readLine();
            }
            MultiSelectionSpinner spinner = (MultiSelectionSpinner) llLayout.findViewById(R.id.IfaceNameSpinner);
            spinner.setSelection(ifaces);

            EditText e = (EditText) llLayout.findViewById(R.id.updateConfDNS1Text);
            e.setEnabled(overrideDNS);
            e = (EditText) llLayout.findViewById(R.id.updateConfDNS2Text);
            e.setEnabled(overrideDNS);
            e = (EditText) llLayout.findViewById(R.id.updateConf_proxy_etr);
            e.setEnabled(!nat_aware);
            e = (EditText) llLayout.findViewById(R.id.updateConfMRText);
            e.setEnabled(!nat_aware);
        } catch (IOException e) {
            Log.e("updateconf", "An IOException was caught :" + e.getMessage());
        }

    }

    public void createConfFile() {
        /*
         * If a configuration file is not found, a default configuration file is created.
		 * */
        try {
            String defText;
            defText = new StringBuilder()
                    .append("#       *** noroot_OOR EXAMPLE CONFIG FILE ***\n\n\n")
                    .append("# General configuration\n")
                    .append("#      debug: Debug levels [0..3]\n")
                    .append("#      map-request-retries: Additional Map-Requests to send per map cache miss\n")
                    .append("#      log-file: Specifies log file used in daemon mode. If it is not specified,\n")
                    .append("#        messages are written in syslog file\n")
                    .append("#      ipv6-scope [GLOBAL|SITE]: Scope of the IPv6 address used for the locators. GLOBAL by default")
                    .append("#      encapsulation: Encapsulation that will use noroot_OOR in the data plane. Could \n")
                    .append("#        be LISP or VXLAN-GPE. LISP is selected by default\n\n")
                    .append("debug                  = " + logLevel + "\n")
                    .append("map-request-retries    = 2\n")
                    .append("log-file               =" +sdcardDir.getAbsolutePath() +"/oor.log\n")
                    .append("ipv6-scope             = GLOBAL\n")
                    .append("encapsulation          = " + encapsulation.toUpperCase() + "\n\n\n")
                    .append("#\n")
                    .append("# operating mode can be any of:\n")
                    .append("# xTR, RTR, MN, MS\n")
                    .append("#\n\n")
                    .append("operating-mode         = MN\n")
                    .append("# RLOC Probing configuration\n")
                    .append("#   rloc-probe-interval: interval at which periodic RLOC probes are sent\n")
                    .append("#     (seconds). A value of 0 disables RLOC Probing\n")
                    .append("#   rloc-probe-retries: RLOC Probe retries before setting the locator with\n")
                    .append("#     status down. [0..5]\n")
                    .append("#   rloc-probe-retries-interval: interval at which RLOC probes retries are\n")
                    .append("#     sent (seconds) [1..#rloc-probe-interval]\n\n")
                    .append("rloc-probing {\n")
                    .append("    rloc-probe-interval             = " + rloc_prob_interval + "\n")
                    .append("    rloc-probe-retries              = " + rloc_prob_retries + "\n")
                    .append("    rloc-probe-retries-interval     = " + rloc_prob_retries_interval + "\n")
                    .append("}\n\n\n")
                    .append("# NAT Traversal configuration. \n")
                    .append("#   nat_traversal_support: check if the node is behind NAT\n\n")
                    .append("nat_traversal_support = " + nat_aware + "\n\n\n")
                    .append("# Encapsulated Map-Requests are sent to this map-resolver\n")
                    .append("# You can define several map-resolvers. Encapsulated Map-Request messages will\n")
                    .append("# be sent to only one.\n")
                    .append("#   address: IPv4 or IPv6 address of the map resolver\n")
                    .append("map-resolver        = {\n")
                    .append("        " + MR + "\n")
                    .append("}\n\n\n")
                    .append("# Map-Registers are sent to this map-server\n")
                    .append("# You can define several map-servers. Map-Register messages will be sent to all\n")
                    .append("# of them.\n")
                    .append("#   address: IPv4 or IPv6 address of the map-server\n")
                    .append("#   key-type: Only 1 supported (HMAC-SHA-1-96)\n")
                    .append("#   key: password to authenticate with the map-server\n")
                    .append("#   proxy-reply [on/off]: Configure map-server to Map-Reply on behalf of the xTR\n\n")
                    .append("map-server {\n")
                    .append("        address     = " + MS + "\n")
                    .append("        key-type    = 1\n")
                    .append("        key         = " + MSKey + "\n")
                    .append("        proxy-reply = on\n")
                    .append("}\n\n\n")
                    .append("# List of PITRs to SMR on handover\n")
                    .append("#   address: IPv4 or IPv6 address of the Proxy-ITR\n")
                    .append("#   Current LISP beta-network (lisp4.net/lisp6.net) PITR addresses\n\n")
                    .append("proxy-itrs = {\n")
                    .append("        69.31.31.98,\n")
                    .append("        129.250.1.63,\n")
                    .append("        217.8.98.33,\n")
                    .append("        217.8.98.35,\n")
                    .append("        193.162.145.46,\n")
                    .append("        147.83.131.33,\n")
                    .append("        158.38.1.92,\n")
                    .append("        203.181.249.172,\n")
                    .append("        202.51.247.10\n")
                    .append("}\n\n\n")
                    .append("# IPv4 / IPv6 EID of the node.\n")
                    .append("# Two kind of rlocs can be defined:\n")
                    .append("#   -> rloc-address: Specifies directly the rloc of the interface\n")
                    .append("#   -> rloc-iface: Specifies the interface associated with the RLOC\n")
                    .append("#\n")
                    .append("#   eid-prefix: EID prefix (IPvX/mask) of the mapping\n")
                    .append("#   address: IPv4 or IPv6 address of the rloc. Address should exist and\n")
                    .append("#      be assigned to an UP interface during starting process otherwise\n")
                    .append("#      it is discarded\n")
                    .append("#   interface: interface containing the RLOCs associated to this mapping\n")
                    .append("#   afi: 4 to use IPv4 address of the interface and 6 to use IPv6 address\n")
                    .append("#     of the interface\n")
                    .append("#   priority [0-255]: Priority for the IPvX RLOC of the interface. Locators\n")
                    .append("#     with lower values are more preferable. This is used for both incoming\n")
                    .append("#     policy announcements and outcoming traffic policy management.\n")
                    .append("#   weight [0-255]: When priorities are the same for multiple RLOCs, the Weight\n")
                    .append("#     indicates how to balance unicast traffic between them.\n")
                    .toString();

            if (ifaces != null) {
                if (!eidIPv4.equals("")) {
                    defText = defText.concat(createEIDConFile(eidIPv4 + "/32"));
                }
                if (!eidIPv6.equals("")) {
                    defText = defText.concat(createEIDConFile(eidIPv6 + "/128"));
                }
            }

            if (!proxyETR.equals("")) {
                String proxyETRstr;
                proxyETRstr = new StringBuilder()
                        .append("# Packets addressed to non-LISP sites will be encapsulated to this Proxy-ETR\n")
                        .append("# You can define several Proxy-ETR for IPv4 EIDs and for IPv6 EIDs. Traffic will\n")
                        .append("# be balanced according to priority and weight.\n")
                        .append("#   address: IPv4 or IPv6 address of the Proxy-ETR\n")
                        .append("#   priority [0-255]: Proxy-ETR with lower values are more preferable.\n")
                        .append("#   weight [0-255]: When priorities are the same for multiple Proxy-ETRs,\n")
                        .append("#     the Weight indicates how to balance unicast traffic between them.\n\n")
                        .append("# Proxy for IPv4 EIDs\n")
                        .append("proxy-etr-ipv4 {\n")
                        .append("        address     = " + proxyETR + "\n")
                        .append("        priority    = 1\n")
                        .append("        weight      = 100\n")
                        .append("}\n\n")
                        .append("# Proxy for IPv6 EIDs\n")
                        .append("proxy-etr-ipv6 {\n")
                        .append("        address     = " + proxyETR + "\n")
                        .append("        priority    = 1\n")
                        .append("        weight      = 100\n")
                        .append("}\n\n\n")
                        .toString();

                defText = defText.concat(proxyETRstr);
            }


            defText = defText.concat("override-dns     		 = " + overrideDNS + "\n");
            if (!DNS1.equals(""))
                defText = defText.concat("override-dns-primary    = " + DNS1 + "\n");
            if (!DNS2.equals(""))
                defText = defText.concat("override-dns-secondary  = " + DNS2 + "\n");

            FileWriter fstream = new FileWriter(conf_file);
            BufferedWriter out = new BufferedWriter(fstream);
            out.write(defText);
            out.close();

        } catch (Exception e) {
            Log.e("conf", e.toString());
        }

    }

    public void createLoginFile(String[] userConf) {

        eidIPv4 = userConf[2];
        eidIPv6 = userConf[3];
        ifaces = new ArrayList<>(Arrays.asList(userConf[4].split(",")));
        MR = userConf[5];
        MS = userConf[6];
        MSKey = userConf[7];
        proxyETR = userConf[8];
        DNS1 = userConf[9];
        DNS2 = userConf[10];
        if (userConf[11].equals("true")) overrideDNS = true;
        else if (userConf[11].equals("false")) overrideDNS = false;
        if (userConf[12].equals("true")) nat_aware = true;
        else if (userConf[12].equals("false")) nat_aware = false;
        logLevel = userConf[13];
        conf_file = new File(sdcardDir, confFile);
        createConfFile();

    }

    private String createEIDConFile(String eid) {
        String eid_statement = new String();
        eid_statement = eid_statement.concat("database-mapping {\n")
                .concat("        eid-prefix     = " + eid + "\n")
                .concat("        iid            = " + iid + "\n");
        Iterator<String> it = ifaces.iterator();
        while (it.hasNext()) {
            String iface_name = it.next();
            eid_statement = eid_statement.concat("        rloc-iface{\n")
                    .concat("           interface     = " + iface_name + "\n")
                    .concat("           ip_version    = 4\n")
                    .concat("           priority      = 1\n")
                    .concat("           weight        = 100\n")
                    .concat("        }\n\n");
            if (nat_aware == false) {
                eid_statement = eid_statement.concat("        rloc-iface{\n")
                        .concat("           interface     = " + iface_name + "\n")
                        .concat("           ip_version    = 6\n")
                        .concat("           priority      = 1\n")
                        .concat("           weight        = 100\n")
                        .concat("        }\n\n");
            }

        }
        eid_statement = eid_statement.concat("}\n");
        return (eid_statement);
    }


    public void displayMessage(String message, boolean cancelAble, final Runnable task) {
        AlertDialog.Builder builder = new AlertDialog.Builder(faActivity);
        builder.setTitle("Attention");
        builder.setMessage(message);
        builder.setCancelable(cancelAble);
        builder.setPositiveButton("OK", new DialogInterface.OnClickListener() {
            public void onClick(DialogInterface dialog, int id) {
                if (task != null) {
                    task.run();
                } else {
                    dialog.dismiss();
                }
            }
        });

        if (cancelAble) {
            builder.setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
                public void onClick(DialogInterface dialog, int id) {
                    dialog.dismiss();
                }
            });
        }

        AlertDialog alert = builder.create();
        alert.show();

    }


    public boolean get_and_validate_parameters() {
        EditText e;
        CheckBox c;
        MultiSelectionSpinner multi_spinner;
        Spinner encapSpinner;
        Spinner logSpinner;
        String iidString = "0";
        String eidv4 = "";
        String eidv6 = "";
        String mapResolver = "";
        String mapServer = "";
        String mapServerKey = "password";
        String pETR = "";
        String DNS_1 = "";
        String DNS_2 = "";
        boolean overrideDNS_bool = false;
        boolean nat_aware_bool = false;


        String message = "ERROR: The following fields are not valid: \n";
        String error = "";


        e = (EditText) llLayout.findViewById(R.id.updateConfeid4Text);
        eidv4 = e.getText().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConfeid6Text);
        eidv6 = e.getText().toString();

        c = (CheckBox) llLayout.findViewById(R.id.updateConf_NAT_aware);
        nat_aware_bool = c.isChecked();

        e = (EditText) llLayout.findViewById(R.id.updateConfMRText);
        mapResolver = e.getText().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConfMSText);
        mapServer = e.getText().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConfMSKeyText);
        mapServerKey = e.getText().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConf_proxy_etr);
        pETR = e.getText().toString();

        c = (CheckBox) llLayout.findViewById(R.id.updateConfDNSCheck);
        overrideDNS_bool = c.isChecked();

        e = (EditText) llLayout.findViewById(R.id.updateConfDNS1Text);
        DNS_1 = e.getText().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConfDNS2Text);
        DNS_2 = e.getText().toString();

        multi_spinner = (MultiSelectionSpinner) llLayout.findViewById(R.id.IfaceNameSpinner);
        ifaces = multi_spinner.getSelectedStrings();

        encapSpinner = (Spinner) llLayout.findViewById(R.id.EncapsulationSpinner);
        encapsulation = encapSpinner.getSelectedItem().toString();

        logSpinner = (Spinner) llLayout.findViewById(R.id.LogSpinner);
        logLevel = logSpinner.getSelectedItem().toString();

        e = (EditText) llLayout.findViewById(R.id.updateConfiidText);
        iidString = e.getText().toString();

        if (!iidString.equals("")) {
            iid = ConfigTools.getInstanceId(iidString);
            if (iid == -1) {
                error = error.concat("  - Instance ID (should be a number)");
                e.setError("should be a number");
            } else if (iid == -2) {
                error = error.concat("  - Instance ID (range is 0 to 2^24)");
                e.setError("range is 0 to 2^24");
            }
        }

        if (!eidv4.equals("") && !ConfigTools.validate_IP_Address(eidv4)) {
            error = error.concat("  - EID-IPv4\n");
        }
        if (!eidv6.equals("") && !ConfigTools.validate_IP_Address(eidv6)) {
            error = error.concat("  - EID-IPv6\n");
        }
        if (nat_aware_bool == false) {
            if (!ConfigTools.validate_IP_Address(mapResolver)) {
                error = error.concat("  - Map-Resolver\n");
            }
            if (!ConfigTools.validate_IP_Address(pETR)) {
                error = error.concat("  - Proxy ETR\n");
            }
        }
        if (!ConfigTools.validate_IP_Address(mapServer)) {
            error = error.concat("  - Map-Server\n");
        }
        if (overrideDNS_bool && (DNS_1.equals("") || !ConfigTools.validate_IP_Address(DNS_1))) {
            error = error.concat("  - Primary DNS\n");
        }
        if ((overrideDNS_bool && !DNS_2.equals("") && !ConfigTools.validate_IP_Address(DNS_2))) {
            error = error.concat("  - Secondary DNS\n");
        }
        if (nat_aware_bool == true && !eidv4.equals("") && !eidv6.equals("")) {
            error = error.concat("  - Only one EID is supported\n");
        }


        if (!error.equals("")) {
            displayMessage(message + error, false, null);
            return (false);
        }
        if (eidv4.equals("") && eidv6.equals("")) {
            displayMessage("ERROR: At least one EID should be supplied", false, null);
            return (false);
        }

        eidIPv4 = eidv4;
        eidIPv6 = eidv6;
        MR = mapResolver;
        MS = mapServer;
        MSKey = mapServerKey;
        proxyETR = pETR;
        DNS1 = DNS_1;
        DNS2 = DNS_2;
        overrideDNS = overrideDNS_bool;
        nat_aware = nat_aware_bool;

        return (true);
    }

    public void updateConfFile() {
        String lastlog = logLevel;
        if (get_and_validate_parameters() == true) {
            createConfFile();
            faActivity.setResult(CONFIG_UPDATED);

            if((lastlog.equals("0") && !logLevel.equals("0")) || (logLevel.equals("0") && !lastlog.equals("0"))) faActivity.recreate();
            Toast.makeText(faActivity, "Configuration updated succesfully", Toast.LENGTH_LONG).show();

        }
    }

    public void updateConfDNSClicked(View v) {
        CheckBox updateCondDNSCheck = (CheckBox) v.findViewById(R.id.updateConfDNSCheck);
        if (updateCondDNSCheck.isChecked()) {
            overrideDNS = true;

            EditText e = (EditText) v.findViewById(R.id.updateConfDNS1Text);
            e.setEnabled(true);

            e = (EditText) v.findViewById(R.id.updateConfDNS2Text);
            e.setEnabled(true);
        } else {
            overrideDNS = false;
            EditText e = (EditText) v.findViewById(R.id.updateConfDNS1Text);
            e.setEnabled(false);

            e = (EditText) v.findViewById(R.id.updateConfDNS2Text);
            e.setEnabled(false);
        }
    }

    public void updateConfNATAwareClicked(View v) {
        CheckBox updateConf_NAT_aware = (CheckBox) v.findViewById(R.id.updateConf_NAT_aware);
        EditText e1 = (EditText) v.findViewById(R.id.updateConf_proxy_etr);
        EditText e2 = (EditText) v.findViewById(R.id.updateConfMRText);
        if (updateConf_NAT_aware.isChecked()) {
            nat_aware = true;
            e1.setEnabled(false);
            e2.setEnabled(false);
        } else {
            nat_aware = false;
            e1.setEnabled(true);
            e2.setEnabled(true);
        }
    }

    public void updateConfClicked(View v) {
        displayMessage("This will overwrite the existing configuration.\nDo you want to Continue?", true, new Runnable() {
            public void run() {
                updateConfFile();
            }
        });
    }

    public void setTextHintAlphas() {
        MaterialEditText aux;
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfeid6Text);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfeid4Text);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfMRText);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfMSText);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfMSKeyText);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConf_proxy_etr);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfDNS1Text);
        aux.setFocusFraction(0.9f);
        aux = (MaterialEditText) llLayout.findViewById(R.id.updateConfDNS2Text);
        aux.setFocusFraction(0.9f);
    }

}
