var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

app.MapGet("/throwinvalidoperation", () =>
{
    throw new System.InvalidOperationException();
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
