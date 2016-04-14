#!/usr/bin/python

import sys, os, json, yaml, math
import urllib

from pyrrd.rrd import DataSource, RRA, RRD
from pyjolokia import Jolokia
from string import maketrans
from numpy import histogram



config = {
    'whisper' : {
        'path': '/var/lib/graphite/whisper/collectd_APPPERF_JMX',
        'prepend': 'collectd_APPPERF_JMX/',

        'addToURL': '?width=586&height=308&from=-6hours&',
        'header': '''<HTML><HEAD></HEAD><BODY>
''',
        'footer': '''</BODY></HTML>'''
        },
    'Weblogic' : {
        'AdminURL'          : 'http://10.50.0.0:7101',
        'JolokiaPath'       : 'jolokia-war-1.2.0/',
        'Hostname'          : '_APPPERF_APP',
        'User'              : 'TheUser',
        'Password'          : 'passvoid'
        },
    'collectd' : {
        'charblacklist'     : '-!. =,#@/()[]',
        'rrd_directory'     : '/var/lib/local_collectd/rrd/'
        },
    'jolokia' : {
        'namespace_whitelist'    : [
            'hip_statistics_performance' # jetm for hip statistics...
            ],
        'interesting_types' : [
            "double",
            "int",
            "java.lang.Double",
            "java.lang.Float",
            "java.lang.Integer",
            "java.lang.Long",
            "long"
#    "java.lang.Boolean":1, # we don't need bolean...
#    "[B":1 # array of byte.., we can't parse this.
            ],
        'uninteresting_beantypes' : [
            "type=Config",
            "type=Compilation"
            ],
        'forbidden_attributes'    : [
            ["name=PS Eden Space,type=MemoryPool","UsageThreshold"],
            ["name=PS Eden Space,type=MemoryPool","UsageThresholdCount"],
            ["name=PS Survivor Space,type=MemoryPool","UsageThreshold"],
            ["name=PS Survivor Space,type=MemoryPool","UsageThresholdCount"],
            ["name=Code Cache,type=MemoryPool","CollectionUsageThreshold"]
            ],
        'interesting_servers'     : [
            "managed1"
            ]
        }
}

def WriteFlatBeanList(filename, Beans):
	'''
		This outputs a CSV in the same format as the perl script used to.
	'''
    f=open(filename, 'w')
    for Bean in Beans:
        f.write (Bean+';'+';'.join(Beans[Bean])+'\n')
    f.close()
	
def ReadBeanCSV(filename):
	'''
		This function reads a CSV file.
	'''
    f=open(filename)
    lines=f.readlines()
    f.close()

    JolokiaRequestStruct=[]
    CollectdConfigStruct=[]
    MixedConfigStruct=[]

    whichline=0

    blacklist     = config['collectd']['charblacklist']
    replacestring ='_' * len(blacklist) # generate nblacklistchar _
    transtab = maketrans(blacklist, replacestring)

    for line in lines:
        beanstruct={}
        try:
            collectd_name=""
            line = line.rstrip('\n')
            parts=line.partition(';')
            attributes=parts[2].rsplit(';')
            bean=parts[0]

			# we try to split the bean into a key value list, so we can use its parts
			#  to find out more about its functions:
			#  the parseable part starts after the first ':'
            beanparts=bean.rsplit(':')[1].rsplit(',')
            namespace=bean.rsplit(':')[0]

            for beancomponent in beanparts:
				# we have a list of key=value strings, split them further.
                beantiles=beancomponent.split('=')
                beanstruct[beantiles[0].lower()] = beantiles[1]
                if beanstruct.has_key('name'):
                    collectd_name = beanstruct['name'].translate(transtab)
                else:
                    collectd_name = bean.rsplit(':')[1].translate(transtab)

			# we use a translation map to replace characters invalid in collectd from the Metric name:
            namespace = namespace.translate(transtab)
			# we implicitely generate the structure which we require for jolokia bulk requests:
			# (later on the json dumper will use it)
            OneJolokiaRequest = {
                "type"      : "read",
                "config"    : {},
                "mbean"     : bean,
                "attribute" : attributes
                }
            JolokiaRequestStruct.append(OneJolokiaRequest)
			# we implicitely generate the structure which we require for collectd configurations
			# (later on the collecd config generator will use this)
            OneCollectdKey = {
                "BeanName" : collectd_name,
                "BeanNameSpace" : namespace,
                "Type" : "gauge",
                "MBean" : bean,
                "Attributes" : attributes
                }
            CollectdConfigStruct.append(OneCollectdKey)

            theattributes={}
            for attribute in attributes:
                path="%s/%s-%s/gauge-%s.rrd" % (
                    config['Weblogic']['Hostname'],
                    namespace,
                    collectd_name,
                    attribute.lower()
                    )
                theattributes[attribute]=path;

			# this is the structure we require to use our black/white list
            OneMixedRequest = {
                "mbean"     : bean,
                "BeanName" : collectd_name,
                "BeanNameSpace" : namespace,
                "Type" : "gauge",
                "attribute" : theattributes
                }

            MixedConfigStruct.append(OneMixedRequest)

            whichline+=1
        except:
            print beanstruct
            print "error parsing line %d [%s]" %(whichline, line)
            raise

    return (JolokiaRequestStruct, CollectdConfigStruct, MixedConfigStruct)

def DumpJolokiaPost(filename, JolokiaRequestStruct):
    # dump awfull json without any useless blanks:
    postdata=json.dumps(JolokiaRequestStruct, separators=(',',':'))
    print("writing [%s]" %(filename))
    f=open(filename, 'w')
    f.write(postdata)
    f.close()

    collectd_jolokia_postdata=postdata.replace('"', '\\"').strip()
    return collectd_jolokia_postdata

def DumpCollectdConfig(filename, CollectdConfigStruct, collectd_jolokia_postdata):
	'''
		This function outputs a collectd configuration which consists of 3 important parts:
		 - Where to locate jolokia (we know this since we also query it)
		 - the big ugly post json with the bulk requests in it
		 - for each bean a mapping from the json to the metric name.
		    (Hint: beans may contain strings which are not valid to collectd metrics)
	'''
    attribute_format = '''\
       <AttributeName "%s" >
              Attribute "%s"
              type "%s"
       </AttributeName>
'''
    bean_format ='''\
  <BeanName "%s">
       MBean "%s"
       BeanNameSpace "%s"
%s
  </BeanName>
'''

    collectdtemplate = '''
# collectd.conf
LoadPlugin "curl_jolokia"
<LoadPlugin curl_jolokia>
     Interval 1
</LoadPlugin>

<Plugin curl_jolokia>
  <URL "%s";>
    Host "%s"
    User "%s"
    Password "%s"
    Post "%s"
%s
   </URL>
</Plugin>
'''

    keys=""
    for BeanStruct in CollectdConfigStruct:
        AttributeStr=""
        for Attribute in BeanStruct["Attributes"]:
            lc_attr = Attribute.lower()
            AttributeStr += (
                attribute_format % (
                    lc_attr,
                    Attribute,
                    BeanStruct['Type']))

        keys += (bean_format % (BeanStruct['BeanName'],
                                BeanStruct['MBean'],
                                BeanStruct['BeanNameSpace'],
                                AttributeStr))

    JolokiaURL = '%s/%s?ignoreErrors=true&canonicalNaming=false' % (
        config['Weblogic']['AdminURL'],
        config['Weblogic']['JolokiaPath']
        )
    print("writing [%s]" %(filename))
    f=open(filename, 'w')
    f.write( collectdtemplate % (
            JolokiaURL,
            config['Weblogic']['Hostname'],
            config['Weblogic']['User'],
            config['Weblogic']['Password'],
            collectd_jolokia_postdata,
            keys) )
    f.close


def GetRRDImportance(filename):
	'''
		This is the very core of cinderella:
		 - we load one RRD into memory
		 - we build a histogram of its values
		 - we use a histogram to find out whether this is an active bean attribute.
	'''
    if not os.path.isfile(filename):
        print("file not found: %s\n" %filename)
        return (0,0)
    #print filename
    myRRD = RRD(filename, mode="r")
    results = myRRD.fetch()['value']
    validnums=[]
    for value in results:
        if not math.isnan(value[1]):# and (value[1] != 0.0):
            validnums.append(value[1])
    #print len(validnums)
    #print validnums
    if len(validnums) > 0:
        h = histogram(validnums)
        #print h
        count = 0

        for counter in h[0]:
            if counter > 0:
                count = count + 1
        only_growing = 1
        last_val = 0
        i = 0
        if count > 2:
            while only_growing and (i < len(validnums)):
                only_growing = validnums[i] > last_val
                last_val = validnums[i]
                i+=1
        return (count, only_growing)
    return (0,0)

def AnalyzeAllRRD(filename, beans):
	'''
		The cinderella job; This function spiders a tree of rrd databases
		in order to find out whether its containing usefull information.
	'''
    num_inspected = 0
    num_usefull = 0
    print("writing [%s]" %(filename))
    f=open(filename, 'w')
    for bean in beans:
        newattributes=[]
        if bean['BeanNameSpace'] in config['jolokia']['namespace_whitelist']:
			# User feedback: X - Whitelist item overriden.
            sys.stdout.write("X")
            continue
		# User feedback: | - starting to process next bean
        sys.stdout.write("|")
        for attribute in bean['attribute']:
            importance = GetRRDImportance(config['collectd']['rrd_directory'] +
                                          bean['attribute'][attribute])

            num_inspected += 1
            if importance[0] > 2:
                num_usefull += 1
                if (importance[1] > 0):
					# User feedback: ; - this one is interesting!
                    sys.stdout.write(";")
                else:
					# User feedback: : - this contains values.
                    sys.stdout.write(":")
                newattributes.append(attribute)
            else:
				# User feedback: . - this is skipped from the final config.
                sys.stdout.write(".")
            sys.stdout.flush()
        sys.stdout.write("\n")
        if len(newattributes) > 0:
            f.write(bean['mbean'] + ";" + ";".join(newattributes) + "\n")
    f.close()

def JolokiaQueryList():
	'''
		This function queries a jolokia for the list of all available beans.
	'''
    # Enter the jolokia url
    JolokiaURL = '%s/%s' % (
        config['Weblogic']['AdminURL'],
        config['Weblogic']['JolokiaPath']
        )
    #print(JolokiaURL)
    j4p = Jolokia(JolokiaURL)
    j4p.auth(httpusername=config['Weblogic']['User'],
             httppassword=config['Weblogic']['Password'])
    
    # Put in the type, the mbean, or other options. Check the jolokia users guide for more info  
    # This then will return back a python dictionary of what happend to the request

#data = j4p.request(type = 'read', mbean='java.lang:type=Threading', attribute='ThreadCount')   
    data = j4p.request(type = 'list', path='')
    return data

def JolokiaParseList(data):
	'''
		This function parses the jolokia list-document and applies black/whitelists.
	'''
    TheValues = data['value']
    TheValueKeys = TheValues.keys()
    InterestingBeans = {}
    #print TheValueKeys
    for BeanNamespace in TheValueKeys:
        #print BeanNamespace
        Beans=TheValues[BeanNamespace].keys()
        #print Beans
        for TheBean in Beans:
            WantAttributes=[]
            beanstruct={}
    
            beanparts=TheBean.rsplit(',')
            for beancomponent in beanparts:
                beantiles=beancomponent.split('=')
                beanstruct[beantiles[0].lower()] = beantiles[1]
            
            if 'visibility' in beanstruct and (beanstruct['visibility'] != 'public'):
                #print 'ignoring [%s] since its private' % TheBean
                continue
            if (('server' in beanstruct) and 
                (not beanstruct['server'] in config['jolokia']['interesting_servers'])):
                continue
            
            if (not TheBean in config['jolokia']['uninteresting_beantypes'] and
                "attr" in TheValues[BeanNamespace][TheBean]):
    
                OneBeanData=TheValues[BeanNamespace][TheBean]["attr"]
                #print json.dumps(OneBeanData)
                AllAttributes=OneBeanData.keys()
                for Attribute in AllAttributes:
                    if OneBeanData[Attribute]['type'] in config['jolokia']['interesting_types']:
                        allowed = True
                        for check in config['jolokia']['forbidden_attributes']:
                            if (check[0] == TheBean) and (check[1] == Attribute):
                                allowed = False
                        if allowed:
                            WantAttributes.append(Attribute)
                        #print(Attribute)
    
                #print(BeanNamespace+":"+TheBean)
                if len(WantAttributes) > 1:
                    InterestingBeans[BeanNamespace+":"+TheBean] = WantAttributes
    return InterestingBeans

def JolokiaParseList_for_desc(data):
	'''
		This function parses the jolokia json tree to find the bean documentations.
	'''
    TheValues = data['value']
    TheValueKeys = TheValues.keys()
    InterestingBeans = {}
    blacklist     = config['collectd']['charblacklist']
    replacestring ='_' * len(blacklist) # generate nblacklistchar _
    transtab = maketrans(blacklist, replacestring)
    for BeanNamespace in TheValueKeys:
        #print BeanNamespace
        Beans=TheValues[BeanNamespace].keys()
        #print Beans
        for TheBean in Beans:
            WantAttributes={}
            beanstruct={}
    
            beanparts=TheBean.rsplit(',')
            for beancomponent in beanparts:
                beantiles=beancomponent.split('=')
                beanstruct[beantiles[0].lower()] = beantiles[1]
            
			# Skip prohibited access items
            if 'visibility' in beanstruct and (beanstruct['visibility'] != 'public'):
                #print 'ignoring [%s] since its private' % TheBean
                continue
				
			# Skip uninteresting servers:
            if (('server' in beanstruct) and 
                (not beanstruct['server'] in config['jolokia']['interesting_servers'])):
                continue
			
            # Skip blacklist items...
            if (not TheBean in config['jolokia']['uninteresting_beantypes'] and
                "attr" in TheValues[BeanNamespace][TheBean]):
    
                OneBeanData=TheValues[BeanNamespace][TheBean]["attr"]
                #print json.dumps(OneBeanData)
                AllAttributes=OneBeanData.keys()
                for Attribute in AllAttributes:
					# skip Attributes from the Attribute-types blacklist:
                    if OneBeanData[Attribute]['type'] in config['jolokia']['interesting_types']:
                        allowed = True
                        for check in config['jolokia']['forbidden_attributes']:
                            if (check[0] == TheBean) and (check[1] == Attribute):
                                allowed = False
                        if allowed and 'desc' in OneBeanData[Attribute]:
                            WantAttributes[str(Attribute).lower()] = OneBeanData[Attribute]['desc']
                        #print(Attribute)
    
                #print(BeanNamespace+":"+TheBean)
                if len(WantAttributes) > 1:
                    beanparts=TheBean.rsplit(',')
                    # another place where we split the bean into its key/values:
                    for beancomponent in beanparts:
                        beantiles=beancomponent.split('=')
                        beanstruct[beantiles[0].lower()] = beantiles[1]
                        if beanstruct.has_key('name'):
                            s=str(beanstruct['name'])
                            collectd_name = s.translate(transtab)
                        else:
                            s=str(TheBean)
                            collectd_name = s.translate(transtab)

					# Filter the namespace for collectd disallowed characters:
                    s=str(BeanNamespace)
                    namespace = s.translate(transtab)

                    if "desc" in TheValues[BeanNamespace][TheBean]:
                        WantAttributes["desc"] = TheValues[BeanNamespace][TheBean]["desc"]
                    if not namespace in InterestingBeans:
                        InterestingBeans[namespace] = dict()
                    InterestingBeans[namespace][collectd_name] = WantAttributes
    return InterestingBeans


def PrintGraphiteURL(GaugeGroup, docu, group):
	'''
	This functions generates the HTML snipet to reference a Bean whith all its attributes.
	It also tries to find the documentation inside of the jolokia browse 
	to add information about which meaning the bean has.
	all attributes of one bean are referenced.
	'''
    print '<hr>'
    HaveDocu = group[0] in docu and group[1] in docu[group[0]]
    if HaveDocu and ('desc' in docu[group[0]][group[1]]) and (docu[group[0]][group[1]]['desc'].find('Deprecation') >= 0):
        beandoc = docu[group[0]][group[1]]['desc']
        parts=beandoc.split('<h3')# throw away st00pit deprecated text which weblogic adds to many jmx documetations
        print '<div>' + parts[0] + '</div>\n'
    for Gauge in GaugeGroup:
		# Each bean can have a set of attributes, which we want to visualise in one graph.
        print '<b>'+Gauge+'</b><br>'
        # try to look up the documentation:
        if HaveDocu:
            gauges=Gauge.split('-')
            TheGauge = gauges[len(gauges)-1]
            if TheGauge in docu[group[0]][group[1]]:
                print '<div>' + docu[group[0]][group[1]][TheGauge] + '</div>'
                print '<br>\n'
            #else:
                #print 'x'*100
                #print docu[group[0]][group[1]].keys()
    URL='/render/' + config['whisper']['addToURL']
	# now the image url to graphites render engine:
    for Gauge in GaugeGroup:
		# One Gauge equals a Bean Attribute:
        graphmetric=Gauge
		# if the attribute is a counter usually derivative delivers better graphs:
        if Gauge.find('count') >= 0:
            graphmetric='derivative('+Gauge+')'
        URL += '&'
        URL += urllib.urlencode({'target':graphmetric})
    print '<img src="%s">\n' % URL

def PrintHTML(dbfiles, basedirectory, prepend):
    jolokia_json_list = JolokiaParseList_for_desc(JolokiaQueryList())
#    print json.dumps(jolokia_json_list)
#    return
    print config['whisper']['header']
    # we iterate over the carbon-cache database
    for directory in dbfiles.keys():
        group = dbfiles[directory]
        GrapName = directory.split('/')

        GrapName=GrapName[len(GrapName)-1]
        GaugeGroup=[]
        beanparts = GrapName.split('-')
        if group:
            for gauge in group:
				# from the gauge name we try to generate its URL in the graphite tree:
                BaseName = prepend + gauge.replace(basedirectory, '')
                GaugeGroup.append(BaseName.rstrip('.wsp').replace('/','.'))
			# We have a set of Attributes, generate one graph:
            PrintGraphiteURL(GaugeGroup, jolokia_json_list, beanparts)
    print config['whisper']['footer']


def BrowseDirectoryStructure(directory):
	'''
		We will spider a carbon-cache database structure and return the tree.
	'''
    MyDirectory=dict()
    TheseFiles=list()
    for subdir, dirs, files in os.walk(directory):
        for TheFile in files:
            TheseFiles.append(directory+'/'+TheFile)
            #.rstrip('.wsp')
        for SubDirectory in dirs:
            subdir = directory+'/'+SubDirectory
            MyDirectory.update(BrowseDirectoryStructure(subdir))
    MyDirectory[directory] = TheseFiles
    return MyDirectory
   
   
def usage( program):
    usage = '''
    Average usage pattern: 
    ./%s query_list test /tmp/
      cut'n'paste the config into .jolokiaclient.yaml, edit enter password etc.
      this time it wrote /tmp/test.txt which contains one Bean per line,
      followed by a ; separated list of Attributes with valuable information.
    
    ./%s generate test /tmp/
      generates /tmp/generated_test.conf to be used with collectd for a test run.
      put this into the site.d directory of a collectd configured to output rrds.
      run collectd, run your testcases.
    
    ./%s.py analyse_rrd generated_test /tmp/
      will now spider all rrd files, analyse whether valuable data is inside,
      and output /tmp/generated_test_reduced.txt
      which only contains the valuable beans & attributes.
      now generate the final collectd config to use with graphite:
    
	./%s.py analyse_whisper 
	  - will first do a jolokia list query
	  - will then spider a carbon cache controlled tree of whisper db files
	  - will output an index.html file with image references to all possible graphs
	    to be generated from beans
		
    ./%s.py generate generated_test_reduced /tmp/
      which will give you /tmp/generated_test_reduced.conf for your production collectd.
    ''' % (program,program,program,program)
    print( usage)
    exit(0)
                                                              
if __name__=='__main__':



    # load our config or dump a sample.
    if len(sys.argv) < 3:
        print '''need at least 3 arguments:
action [generate|analyse_rrd|query_list]
Basefilename
directory
'''
        usage()
        exit(1)
    try:
        cfgfile = open('.jolokiaclient.yaml', 'r')
    except:
        print '\nno config ".jolokiaclient.yaml" found. Printing sample config and exit.\n\n'
        print (yaml.safe_dump(config, default_flow_style=False))
        
        exit(1)

    confstr = cfgfile.read()
    cfgfile.close()
    config = yaml.safe_load(confstr)

    basename=sys.argv[2]

    directory='.'
    directory=sys.argv[3]

    FlatBeanListFile="%s/%s.txt" %(directory, basename)
    collectd_outputname="%s/generated_%s.conf" %(directory, basename)
    outputpostdata="%s/post_%s.json" %(directory, basename)
    outputpersistance="%s/%s.json" %(directory, basename)
    outputreduced="%s/%s_reduced.txt" %(directory, basename)

    # First step : get the list of all JMX beans
    if (sys.argv[1] == 'query_list'):
        # Jolokia gives us a list of all JMX available:
        jolokia_json_list = JolokiaQueryList()
        # we need to parse it, and evaluate the ones which contain valueable information
        # - we will filter out values which don't contain numbers
        # - we will filter out configuration values
        # - we will filter out the blacklist we have.
        FlatBeanList = JolokiaParseList(jolokia_json_list)
        WriteFlatBeanList(FlatBeanListFile, FlatBeanList)
		# we will output a list of JMX in the CSV-syntax of the perl script:
		# <bean name>;metric1;metric2;...
		# this file will be used for subsequent operations.
    elif sys.argv[1] == 'generate':
		# this step generates the jolokia bulk request from the CSV above.
		#   First read the CSV from disk:
        (JolokiaRequestStruct,
         CollectdConfigStruct,
         MixedConfigStruct) = ReadBeanCSV(FlatBeanListFile)
		 
		# now we know the metrics, we generate the Jolokia bulk request:
        collectd_jolokia_postdata = DumpJolokiaPost(outputpostdata,
                                                    JolokiaRequestStruct)

		# collectd needs the Jolokia-post data, plus the mapping from Bean->grahpite metric:
        DumpCollectdConfig(collectd_outputname,
                           CollectdConfigStruct,
                           collectd_jolokia_postdata)
						   
		# We also output the json post data to disk, just in case you want to test it with cURL or such:
        print("writing [%s]" %(outputpersistance))
        f = open(outputpersistance, 'w')
        f.write(json.dumps(MixedConfigStruct))
        f.close()

    elif sys.argv[1] == 'analyse_rrd':
		# this is cinderella. here we try to find the metrics which contain usefull values.
		# first we load our CSV again:
        (JolokiaRequestStruct, CollectdConfigStruct, MixedConfigStruct) = ReadBeanCSV(FlatBeanListFile)
		# then we filter them according to our whitelist and mathematical analysis:
        AnalyzeAllRRD(outputreduced, MixedConfigStruct)
    elif sys.argv[1] == 'analyse_whisper':
		# in this step we generate an index.html to referenece all metrics so we can see all results
		# in one browser window without clicking together the graphs in the graphite webinterface.
		# the index.html is intendet to be put into the graphite web service.
		#  we also use jolokia to get the text information about which information is contained in the 
		#  jmx counters.
        dbfiles = BrowseDirectoryStructure(config['whisper']['path'])
        dbfiles[config['whisper']['path']] = None
        # this outputs the html:
        PrintHTML(dbfiles, config['whisper']['path'] + '/', config['whisper']['prepend'])
