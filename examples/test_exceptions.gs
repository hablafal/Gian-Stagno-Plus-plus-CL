fn test_exc():
    println_string("Before raise")
    raise "My Error"
    println_string("After raise (should not see this)")

fn main():
    try:
        test_exc()
    except Exception as e:
        println_string("Caught exception:")
        println_string(e)
    finally:
        println_string("In finally block")
    println_string("After try-except")
