    echo off
    cd \fbb                                               
    break off
:start
    echo Program starts, use ^C to stop within 3 seconds...
    sleep 3
	fbbios 1 03F8 4
    serv %1
    if errorlevel 2 goto direct
    if errorlevel 1 goto suite
    goto fin
:suite
    echo Cleaning up messages...
    del epurmess.res
    epurmess
    epurwp 14 365
    cd \fbb\system
    clr_user
    MAINTINF 3 /a
    COPY c:\FBB\SYSTEM\INF.NEW c:\FBB\SYSTEM\INF.SYS
    cd \fbb
    del c:\fbb\system\dirmes.new
    del c:\fbb\system\dirmes.old
    rem if not exist c:\fbb\error.sys goto direct
    rem echo SP LA6CU             >> \FBB\MAIL.IN
    rem echo Error-report         >> \FBB\MAIL.IN
    rem type \FBB\ERROR.SYS       >> \FBB\MAIL.IN
    rem echo.                     >> \FBB\MAIL.IN
    rem ECHO /EX                  >> \FBB\MAIL.IN
    rem type \FBB\ERROR.SYS       >> \FBB\ERROR.LOG
    rem del \FBB\ERROR.SYS
:direct
    cd \fbb\system\sat                                         
    if not exist amsat.txt goto appel                          
    satupdat amsat.txt /n > satupdat.res
    satupdat amsat.txt >> satupdat.res
    del amsat.txt
:appel
    cd \fbb
    echo Program starts, use ^C to interrupt within 3 seconds..
    sleep 3
    serv -t
rem -t means "Start program, but do not write in ERROR.SYS"
    if errorlevel 2 goto direct
    if errorlevel 1 goto suite
:fin
    echo Program terminates..
    break on
    Echo on
