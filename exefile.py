import subprocess
import os
import sys

# sys.argv[1] = win32|x64
# sys.argv[2] = release|debug|releasebreakpad

# --------------------------------------------------------------------------------
configuration = sys.argv[2].lower()
if configuration == "debug":
    EXEFILE = "exefile_d"
elif configuration == 'releasebreakpad':
    EXEFILE = "exefilebreakpad"
else:
    EXEFILE = "exefile"

# setting variables
ROBOCOPY = r"..\..\..\..\..\..\shared_tools\utils\robocopy.exe"
ICONTOOL = r"..\..\..\..\..\..\shared_tools\utils\ReplaceVistaIcon.exe"
TIMESITE = r"http://timestamp.verisign.com/scripts/timstamp.dll"
SIGNTOOL = r"..\..\..\..\..\..\shared_tools\utils\signtool\signtool.exe"
SIGNCERT = r"..\..\..\..\..\..\shared_tools\utils\signtool\CodeSign.pfx"

signaturepwd = os.getenv("CODESIGN_PWD")

# --------------------------------------------------------------------------------
def robocopy(project):
    print "Copying binaries for %s" % project
    with open(os.devnull) as logout:
        p = subprocess.Popen("%(robocopy)s /PURGE ../../autobuild/exefile/%(arg1)s ../../../%(project)s/autobuild/exefile/%(arg1)s %(exefile)s.exe %(exefile)s.pdb" % {"project" : project, "robocopy" : ROBOCOPY, "exefile" : EXEFILE, "arg1" : sys.argv[1].lower()}, stdout=logout)
        
        returncode = p.wait()
        if returncode > 4:
            print "Error copying binaries for %s" % project
            sys.exit(1)
            
# --------------------------------------------------------------------------------
def signing(project):
    print "Signing binaries for %s" % project
    retry = 0
    signed = False
    while signed == False:
        if retry > 0: print "signing %s binares attempt %s" % (project, retry)
        with open(os.devnull) as logout:
            p = subprocess.Popen(r"%s sign /f %s /t %s /p %s ..\..\..\%s\autobuild\exefile\%s\%s.exe" % (SIGNTOOL, SIGNCERT, TIMESITE, signaturepwd, project, sys.argv[1].lower(), EXEFILE), stdout=logout, stderr = subprocess.PIPE)
            
            if p.stderr:
                print p.stderr.read()
        
            returncode = p.wait()
            if returncode == 0:
                signed = True
            
            if retry > 10:
                sys.exit("Error signing %s binaries" % (project))
                break
                
            retry += 1
            
# --------------------------------------------------------------------------------
def updateIcon(project):
    print "Updating the icon for %s" % project
    with open(os.devnull) as logout:
        p = subprocess.Popen("%s ../../../%s/autobuild/exefile/%s/%s.exe %s.ico" % (ICONTOOL, project, sys.argv[1].lower(), EXEFILE, project), stdout=logout)
        
        returncode = p.wait()
        if returncode != 0:
            print "Error signing %s icon" % project
            sys.exit(1)

# --------------------------------------------------------------------------------            
if __name__ == "__main__":

    if len(sys.argv) > 1:
        print "Updating the icon for Carbon"
        with open(os.devnull) as logout:
            p = subprocess.Popen("%s ../../autobuild/exefile/%s/%s.exe carbonIconFinal.ico" % (ICONTOOL, sys.argv[1].lower(), EXEFILE), stdout=logout)
        
            returncode = p.wait()
            if returncode != 0:
                print "Error signing carbon icon"
                sys.exit(1)
            
        for project in ('eve', 'wod'):
            if os.path.exists('../../../%s' % (project)):
                robocopy(project)
                updateIcon(project)
                if signaturepwd:
                    signing(project)
     
    else:
        sys.exit("Error doing something")

    sys.exit(0)

    
