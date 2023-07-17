using System.Runtime.InteropServices;

var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

app.MapGet("/throwinvalidoperation", () =>
{
    throw new System.InvalidOperationException();
});

app.MapGet("/fullgc", () =>
{
    System.GC.Collect();
});

app.MapGet("/memincrease", () =>
{
    // Gen2
    var myList = new List<byte[]>();
    for(int i = 0; i < 1000; i++)
    {
        myList.Add(new byte[10000]);
    }
    // Promote to Gen2
    GC.Collect();
    GC.Collect();
    GC.Collect();

    // LOH
    var myLOHList = new List<byte[]>();
    myLOHList.Add(new byte[15000000]);
    System.GC.Collect();
    myLOHList.Add(new byte[15000000]);
    System.GC.Collect();
    myLOHList.Add(new byte[15000000]);
    System.GC.Collect();

    // POH
    var p1 = GC.AllocateArray<byte>(15000000, pinned: true);
    System.GC.Collect();
    var p2 = GC.AllocateArray<byte>(15000000, pinned: true);
    System.GC.Collect();
    var p3 = GC.AllocateArray<byte>(15000000, pinned: true);
    System.GC.Collect();
});


app.MapGet("/throwandcatchinvalidoperation", () =>
{
    try
    {
        throw new System.InvalidOperationException();
    }
    catch(Exception){}

    throw new System.InvalidOperationException();
});


app.MapGet("/throwargumentexception", () =>
{
    throw new System.ArgumentException();
});

// Kills the web api
app.MapGet("/terminate", () =>
{
    System.Environment.Exit(0);
});

// Kills the web api
app.MapGet("/stress", () =>
{
    List<Thread> arr = new List<Thread>();
    for(int i=0; i<50; i++)
    {
        arr.Add(new Thread(DoWork));
    }

    foreach(Thread thread in arr)
    {
        thread.Start();
    }
});

void DoWork()
{
    for(int i = 0; i<50;i++)
    {
        try
        {
            throw new System.InvalidOperationException();
        }
        catch(Exception){}
    }
}

app.Run();
