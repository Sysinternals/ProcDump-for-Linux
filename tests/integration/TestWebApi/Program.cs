var builder = WebApplication.CreateBuilder(args);
var app = builder.Build();

app.MapGet("/throwinvalidoperation", () =>
{
    Console.WriteLine("AHA");
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




app.Run();
