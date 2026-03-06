from rest_framework import generics
from .models import AstroObject
from .serializers import AstroObjectSerializer

class AstroObjectList(generics.ListAPIView):
    queryset = AstroObject.objects.all()
    serializer_class = AstroObjectSerializer

