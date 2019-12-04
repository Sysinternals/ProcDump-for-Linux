# Procdump and .NET Core 3 Integration

Procdump is a powerful production diagnostics tool that allows you to monitor processes for specific thresholds and generate core dumps based on a specified critera. For example, imagine that you were encountering sporadic CPU spikes in your web app and you would like to generate a core dump for offline analysis. With Procdump you can use the -C switch to specify the CPU threshold of interest (say, 90%) and Procdump will monitor the process and generate a core dump when the CPU goes above 90%.

In order to understand how a core dump can help resolve production issues, we first have to understand what a core dump is. Essentially, a core dump is nothing more than a static snapshot of the contents of an applications memory. This content is written to a file that can later be loaded into a debugger and other tools to analyze the contents of memory and see if root cause can be determined. What makes core dumps great production debugging tools is that we don't have to worry about the application stopping while debugging is taking place (as is the case with live debugging where a debugger is attached to the application). How big are these dump files? Well, that depends on how much memory your application is consuming (remember, it roughly writes the contents of the application memory usage to the file). As you can imagine, if you are running a massive database application you could result in a core dump file that is many GB in size. Furthermore, on Linux, core dumps tend to be larger than on Windows. This presents a new challenge of how to effectively manage large core dump files. If you intend to copy the core dump file between production and development machines, it can be pretty time consuming.

With the release of Procdump 1.1, we now recognize if the target application is a .NET Core application and use a special core dumping mechanism built into the runtime itself. This renders core dump files that are much smaller than normal while still maintaining all the neccessary information to troubleshoot the .NET Core application. You don't have to specify any new switches to use this new capability, Procdump automatically figures out if the capability is available and uses it.

To get a feel for the size difference, let's take a look at a simple example. We created and ran a new .NET Core app using the webapp template:

```console
dotnet new webapp -o TestWebApp
dotnet run
```

This webapp does very little and as a matter of fact we won't even send any requests to it. Now, using Procdump (**1.0**), if we generate a core dump of the web app we end up with a file that is roughly 18GB in size. Pretty hefty for a simple web app that essentially does nothing.

```console
-rw-r--r--  1 root   root   18127292104 Dec  4 11:37 core.3066
```

Using the same web app, let's use Procdump **1.1** to generate the core dump. This time the file size is much more reasonable:

```console
-rw-rw-r-- 1 marioh marioh 273387520 Dec  4 11:44 TestWebApp_time_2019-12-04_11:44:03.3066
```

_Please note that by default the core dump will be placed into the same directory that the target application is running in._

This time the core dump file size is only about 273MB. Much better and much more managable. To convince ourselves that the core dump still contains all the neccessary data to debug .NET Core 3.0 applications, we can try it out with dotnet-dump analyze (which is a REPL for SOS debugging):

```console
dotnet-dump analyze TestWebApp_time_2019-12-04_11:44:03.3066
> dumpheap -stat
Statistics:
              MT    Count    TotalSize Class Name
00007f7b2f5e4288        1           24 System.Threading.TimerQueueTimer+<>c
00007f7b2f5e2738        1           24 System.Net.Sockets.SocketAsyncEngine+<>c
...
...
...
00007f7b2bcd4a18      397       109816 System.Char[]
00007f7b2b1514c0      628       110601 System.Byte[]
00007f7b2b145510      509       166528 System.Object[]
00007f7b2b150f90     4436       342956 System.String
Total 32581 objects
```

How does Procdump achieve this magic? Turns out that .NET Core 3.0 introduced the notion of a diagnostics server which at a high level enables external (out of process) tools to send diagnostics commands to the target process. In our case, we used the dump commands that are available but you can also use the diagnostics server to issue trace commands. For more information, please see the following documentation:

[.NET Core Diagnostics](https://github.com/dotnet/diagnostics)

