#!/usr/bin/python

import subprocess;
import os;

try :
	print subprocess.check_output('./fastzip infozip.zip -8 src/infozip'.split())
	print subprocess.check_output('./fastzip obj.zip -0 obj'.split())
	print subprocess.check_output('./fastzip -j -v --sign -Z infozip.zip igzip.zip src/igzip -0 -Z obj.zip fastzip=fs.stored -9 fastzip=packed/fastzip'.split())
	print subprocess.check_output('unzip -lv igzip.zip'.split())
	output = subprocess.check_output('unzip -t igzip.zip'.split())
	if output.find('No errors detected') >= 0 :
		print "Zip file OK"
                os.remove('obj.zip')
                os.remove('infozip.zip')
	else :
		print output
	output = subprocess.check_output('jarsigner -verbose -certs -verify igzip.zip'.split())
	if output.find('jar verified') >= 0 :
		print "Signing verfied OK"
                os.remove('igzip.zip')
	else :
		print output
except subprocess.CalledProcessError :
	print "Duh!"
