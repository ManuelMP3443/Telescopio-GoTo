from django.urls import path
from .views import AstroObjectList

urlpatterns = [
    path('estrellas/', AstroObjectList.as_view(), name='astro-object-list'),
]