def reverse_string(s: str) -> str:
    """Return the reverse of the input string s.

    Args:
        s: The string to reverse.

    Returns:
        A new string which is the reverse of s.
    """
    return s[::-1]

# Example usage
if __name__ == "__main__":
    test = "Hello, World!"
    print(f"Original: {test}")
    print(f"Reversed: {reverse_string(test)}")
